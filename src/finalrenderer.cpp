#include "finalrenderer.h"

#include <QBuffer>
#include <QColor>
#include <QFileInfo>
#include <QImage>
#include <QQuaternion>
#include <QSaveFile>
#include <QVariantList>
#include <QVector2D>
#include <QVector3D>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <utility>
#include <vector>

namespace
{
constexpr float kRayEpsilon = 1.0e-3f;
constexpr int kLeafTriangleCount = 4;
constexpr qint64 kMaximumRenderPixels = 33554432;
constexpr qint64 kMaximumSceneTriangles = 2000000;
constexpr float kPi = 3.14159265358979323846f;

bool finiteVector(const QVector3D &value)
{
    return std::isfinite(value.x()) && std::isfinite(value.y())
        && std::isfinite(value.z());
}

double numberOr(const QVariantMap &map, const QString &key, double fallback)
{
    if (!map.contains(key))
        return fallback;
    bool ok = false;
    const double value = map.value(key).toDouble(&ok);
    return ok && std::isfinite(value) ? value : fallback;
}

float srgbToLinear(float value)
{
    return value <= 0.04045f ? value / 12.92f
                             : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float value)
{
    value = std::max(0.0f, value);
    return value <= 0.0031308f ? value * 12.92f
                               : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

QVector3D colorToLinear(const QColor &color)
{
    return QVector3D(srgbToLinear(color.redF()),
                     srgbToLinear(color.greenF()),
                     srgbToLinear(color.blueF()));
}

QVector3D componentMultiply(const QVector3D &a, const QVector3D &b)
{
    return QVector3D(a.x() * b.x(), a.y() * b.y(), a.z() * b.z());
}

float largestAxis(const QVector3D &extent)
{
    return std::max({extent.x(), extent.y(), extent.z()});
}

struct Ray
{
    QVector3D origin;
    QVector3D direction;
};

struct Bounds
{
    QVector3D minimum{
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity()
    };
    QVector3D maximum{
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity()
    };

    void include(const QVector3D &point)
    {
        minimum.setX(std::min(minimum.x(), point.x()));
        minimum.setY(std::min(minimum.y(), point.y()));
        minimum.setZ(std::min(minimum.z(), point.z()));
        maximum.setX(std::max(maximum.x(), point.x()));
        maximum.setY(std::max(maximum.y(), point.y()));
        maximum.setZ(std::max(maximum.z(), point.z()));
    }

    void include(const Bounds &other)
    {
        include(other.minimum);
        include(other.maximum);
    }

    QVector3D extent() const { return maximum - minimum; }
    QVector3D centroid() const { return (minimum + maximum) * 0.5f; }
};

struct Triangle
{
    QVector3D a;
    QVector3D b;
    QVector3D c;
    QVector3D albedo;
    QVector3D centroid;
    Bounds bounds;
    float roughness = 0.38f;
};

struct Hit
{
    float distance = std::numeric_limits<float>::infinity();
    int triangleIndex = -1;
    QVector3D position;
    QVector3D normal;
};

struct BvhNode
{
    Bounds bounds;
    int left = -1;
    int right = -1;
    int first = 0;
    int count = 0;

    bool isLeaf() const { return left < 0; }
};

bool intersectsBounds(const Ray &ray,
                      const Bounds &bounds,
                      float maxDistance)
{
    float nearDistance = 0.0f;
    float farDistance = maxDistance;

    const std::array<float, 3> origin{
        ray.origin.x(), ray.origin.y(), ray.origin.z()
    };
    const std::array<float, 3> direction{
        ray.direction.x(), ray.direction.y(), ray.direction.z()
    };
    const std::array<float, 3> minimum{
        bounds.minimum.x(), bounds.minimum.y(), bounds.minimum.z()
    };
    const std::array<float, 3> maximum{
        bounds.maximum.x(), bounds.maximum.y(), bounds.maximum.z()
    };

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1.0e-9f) {
            if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis])
                return false;
            continue;
        }

        const float inverse = 1.0f / direction[axis];
        float first = (minimum[axis] - origin[axis]) * inverse;
        float second = (maximum[axis] - origin[axis]) * inverse;
        if (first > second)
            std::swap(first, second);

        nearDistance = std::max(nearDistance, first);
        farDistance = std::min(farDistance, second);
        if (farDistance < nearDistance)
            return false;
    }

    return farDistance >= kRayEpsilon;
}

bool intersectTriangle(const Ray &ray,
                       const Triangle &triangle,
                       float maxDistance,
                       float *distance)
{
    const QVector3D edge1 = triangle.b - triangle.a;
    const QVector3D edge2 = triangle.c - triangle.a;
    const QVector3D p = QVector3D::crossProduct(ray.direction, edge2);
    const float determinant = QVector3D::dotProduct(edge1, p);

    if (std::abs(determinant) < 1.0e-8f)
        return false;

    const float inverse = 1.0f / determinant;
    const QVector3D t = ray.origin - triangle.a;
    const float u = QVector3D::dotProduct(t, p) * inverse;
    if (u < 0.0f || u > 1.0f)
        return false;

    const QVector3D q = QVector3D::crossProduct(t, edge1);
    const float v = QVector3D::dotProduct(ray.direction, q) * inverse;
    if (v < 0.0f || u + v > 1.0f)
        return false;

    const float hitDistance = QVector3D::dotProduct(edge2, q) * inverse;
    if (hitDistance <= kRayEpsilon || hitDistance >= maxDistance)
        return false;

    *distance = hitDistance;
    return true;
}

bool readVector3(const QVariant &raw, QVector3D *value)
{
    const QVariantList components = raw.toList();
    if (components.size() != 3)
        return false;

    double coordinate[3] = {};
    for (int axis = 0; axis < 3; ++axis) {
        bool ok = false;
        coordinate[axis] = components.at(axis).toDouble(&ok);
        if (!ok || !std::isfinite(coordinate[axis])
            || std::abs(coordinate[axis]) > 1.0e6) {
            return false;
        }
    }

    *value = QVector3D(static_cast<float>(coordinate[0]),
                       static_cast<float>(coordinate[1]),
                       static_cast<float>(coordinate[2]));
    return finiteVector(*value);
}

bool appendSceneTriangles(const QVariantMap &scene,
                          std::vector<Triangle> *triangles,
                          QString *error)
{
    const QVariantList objects = scene.value(QStringLiteral("objects")).toList();
    if (objects.isEmpty()) {
        *error = QStringLiteral("The scene contains no objects to render.");
        return false;
    }
    if (objects.size() > 10000) {
        *error = QStringLiteral("The scene exceeds the 10,000-object render limit.");
        return false;
    }

    for (const QVariant &rawObject : objects) {
        const QVariantMap object = rawObject.toMap();
        if (object.value(QStringLiteral("kind")).toString() == QLatin1String("asset"))
            continue; // Runtime-loaded glTF is not embedded in the native scene mesh yet.

        const QVariantMap mesh = object.value(QStringLiteral("mesh")).toMap();
        const QVariantList rawVertices = mesh.value(QStringLiteral("vertices")).toList();
        const QVariantList rawFaces = mesh.value(QStringLiteral("faces")).toList();
        if (rawVertices.size() < 3 || rawFaces.isEmpty())
            continue;

        QVector<QVector3D> localVertices;
        localVertices.reserve(rawVertices.size());
        for (const QVariant &rawVertex : rawVertices) {
            QVector3D vertex;
            if (!readVector3(rawVertex, &vertex)) {
                *error = QStringLiteral("Object '%1' has invalid mesh coordinates.")
                             .arg(object.value(QStringLiteral("label")).toString());
                return false;
            }
            localVertices.append(vertex);
        }

        const double x = numberOr(object, QStringLiteral("x"), 0.0);
        const double y = numberOr(object, QStringLiteral("y"), 0.0);
        const double z = numberOr(object, QStringLiteral("z"), 0.0);
        const double rx = numberOr(object, QStringLiteral("rx"), 0.0);
        const double ry = numberOr(object, QStringLiteral("ry"), 0.0);
        const double rz = numberOr(object, QStringLiteral("rz"), 0.0);
        const double scale = numberOr(object, QStringLiteral("scale"), 1.0);
        if (std::abs(x) > 1.0e6 || std::abs(y) > 1.0e6 || std::abs(z) > 1.0e6
            || std::abs(rx) > 1.0e6 || std::abs(ry) > 1.0e6 || std::abs(rz) > 1.0e6
            || scale <= 0.0001 || scale > 10000.0) {
            *error = QStringLiteral("Object '%1' has an invalid transform.")
                         .arg(object.value(QStringLiteral("label")).toString());
            return false;
        }

        const QVector3D position(static_cast<float>(x),
                                 static_cast<float>(y),
                                 static_cast<float>(z));
        const QQuaternion rotation = QQuaternion::fromEulerAngles(
            static_cast<float>(rx), static_cast<float>(ry), static_cast<float>(rz));

        QString colorText = object.value(QStringLiteral("color")).toString();
        if (colorText.isEmpty())
            colorText = QStringLiteral("#c8c8c8");
        QColor color(colorText);
        if (!color.isValid())
            color = QColor(QStringLiteral("#c8c8c8"));
        const QVector3D albedo = colorToLinear(color);
        const float roughness = static_cast<float>(
            std::clamp(numberOr(object, QStringLiteral("roughness"), 0.38), 0.0, 1.0));

        for (const QVariant &rawFace : rawFaces) {
            const QVariantList corners = rawFace.toList();
            if (corners.size() < 3 || corners.size() > 4096)
                continue;

            QVector<int> face;
            face.reserve(corners.size());
            for (const QVariant &rawIndex : corners) {
                bool ok = false;
                const int index = rawIndex.toInt(&ok);
                if (!ok || index < 0 || index >= localVertices.size()) {
                    *error = QStringLiteral("Object '%1' contains an invalid face index.")
                                 .arg(object.value(QStringLiteral("label")).toString());
                    return false;
                }
                face.append(index);
            }

            auto worldVertex = [&](int index) {
                return position + rotation.rotatedVector(localVertices.at(index)
                                                           * static_cast<float>(scale));
            };
            const QVector3D a = worldVertex(face.first());
            for (int corner = 1; corner + 1 < face.size(); ++corner) {
                if (static_cast<qint64>(triangles->size()) >= kMaximumSceneTriangles) {
                    *error = QStringLiteral("The scene exceeds the 2,000,000-triangle render limit.");
                    return false;
                }

                Triangle triangle;
                triangle.a = a;
                triangle.b = worldVertex(face.at(corner));
                triangle.c = worldVertex(face.at(corner + 1));
                const QVector3D areaVector =
                    QVector3D::crossProduct(triangle.b - triangle.a,
                                            triangle.c - triangle.a);
                if (!finiteVector(areaVector) || areaVector.lengthSquared() < 1.0e-10f)
                    continue;

                triangle.albedo = albedo;
                triangle.roughness = roughness;
                triangle.centroid = (triangle.a + triangle.b + triangle.c) / 3.0f;
                triangle.bounds.include(triangle.a);
                triangle.bounds.include(triangle.b);
                triangle.bounds.include(triangle.c);
                // Padding avoids missed hits from rays grazing triangle bounds.
                const QVector3D padding(1.0e-4f, 1.0e-4f, 1.0e-4f);
                triangle.bounds.minimum -= padding;
                triangle.bounds.maximum += padding;
                triangles->push_back(std::move(triangle));
            }
        }
    }

    if (triangles->empty()) {
        *error = QStringLiteral("No editable mesh geometry was available to render. Runtime-loaded GLB/glTF assets are currently read-only and skipped.");
        return false;
    }
    return true;
}

class SceneAccelerator
{
public:
    explicit SceneAccelerator(std::vector<Triangle> sceneTriangles)
        : triangles(std::move(sceneTriangles))
    {
        order.resize(triangles.size());
        std::iota(order.begin(), order.end(), 0);
        if (!order.empty())
            buildNode(0, static_cast<int>(order.size()));
    }

    bool closestHit(const Ray &ray, Hit *hit) const
    {
        if (nodes.empty())
            return false;

        bool found = false;
        // The BVH is balanced, so a fixed traversal stack avoids a heap
        // allocation for every primary and shadow ray in high-resolution renders.
        std::array<int, 128> pending{};
        int pendingCount = 0;
        pending[pendingCount++] = 0;

        while (pendingCount > 0) {
            const int nodeIndex = pending[--pendingCount];
            const BvhNode &node = nodes.at(nodeIndex);
            if (!intersectsBounds(ray, node.bounds, hit->distance))
                continue;

            if (node.isLeaf()) {
                for (int item = 0; item < node.count; ++item) {
                    const int triangleIndex = order.at(node.first + item);
                    float distance = 0.0f;
                    if (intersectTriangle(ray, triangles.at(triangleIndex),
                                          hit->distance, &distance)) {
                        hit->distance = distance;
                        hit->triangleIndex = triangleIndex;
                        hit->position = ray.origin + ray.direction * distance;
                        QVector3D normal = QVector3D::crossProduct(
                            triangles.at(triangleIndex).b - triangles.at(triangleIndex).a,
                            triangles.at(triangleIndex).c - triangles.at(triangleIndex).a);
                        if (normal.lengthSquared() > 1.0e-10f)
                            normal.normalize();
                        if (QVector3D::dotProduct(normal, ray.direction) > 0.0f)
                            normal = -normal;
                        hit->normal = normal;
                        found = true;
                    }
                }
            } else {
                // With a median-split BVH built from at most two million
                // triangles, traversal depth is well below this fixed capacity.
                if (pendingCount + 2 <= static_cast<int>(pending.size())) {
                    pending[pendingCount++] = node.left;
                    pending[pendingCount++] = node.right;
                }
            }
        }
        return found;
    }

    bool occluded(const Ray &ray, float maxDistance) const
    {
        if (nodes.empty())
            return false;

        std::array<int, 128> pending{};
        int pendingCount = 0;
        pending[pendingCount++] = 0;
        while (pendingCount > 0) {
            const int nodeIndex = pending[--pendingCount];
            const BvhNode &node = nodes.at(nodeIndex);
            if (!intersectsBounds(ray, node.bounds, maxDistance))
                continue;

            if (node.isLeaf()) {
                for (int item = 0; item < node.count; ++item) {
                    const Triangle &triangle = triangles.at(order.at(node.first + item));
                    float distance = 0.0f;
                    if (intersectTriangle(ray, triangle, maxDistance, &distance))
                        return true;
                }
            } else {
                if (pendingCount + 2 <= static_cast<int>(pending.size())) {
                    pending[pendingCount++] = node.left;
                    pending[pendingCount++] = node.right;
                }
            }
        }
        return false;
    }

    const Triangle &triangle(int index) const { return triangles.at(index); }
    int triangleCount() const { return static_cast<int>(triangles.size()); }

private:
    std::vector<Triangle> triangles;
    std::vector<int> order;
    std::vector<BvhNode> nodes;

    int buildNode(int first, int count)
    {
        const int nodeIndex = static_cast<int>(nodes.size());
        nodes.emplace_back();

        Bounds bounds;
        Bounds centroidBounds;
        for (int index = first; index < first + count; ++index) {
            const Triangle &triangle = triangles.at(order.at(index));
            bounds.include(triangle.bounds);
            centroidBounds.include(triangle.centroid);
        }

        nodes[nodeIndex].bounds = bounds;
        nodes[nodeIndex].first = first;
        nodes[nodeIndex].count = count;
        if (count <= kLeafTriangleCount)
            return nodeIndex;

        const QVector3D extent = centroidBounds.extent();
        int axis = 0;
        if (extent.y() > extent.x())
            axis = 1;
        if ((axis == 0 ? extent.x() : extent.y()) < extent.z())
            axis = 2;

        if (largestAxis(extent) < 1.0e-7f)
            return nodeIndex;

        auto coordinate = [axis](const QVector3D &value) {
            if (axis == 0) return value.x();
            if (axis == 1) return value.y();
            return value.z();
        };

        const int middle = first + count / 2;
        std::nth_element(order.begin() + first, order.begin() + middle,
                         order.begin() + first + count,
                         [&](int a, int b) {
                             return coordinate(triangles.at(a).centroid)
                                 < coordinate(triangles.at(b).centroid);
                         });

        nodes[nodeIndex].count = 0;
        const int left = buildNode(first, middle - first);
        const int right = buildNode(middle, first + count - middle);
        nodes[nodeIndex].left = left;
        nodes[nodeIndex].right = right;
        return nodeIndex;
    }
};

QVector3D directLighting(const Ray &ray,
                         const Hit &hit,
                         const SceneAccelerator &scene,
                         const QVector3D &directionalLight,
                         float keyBrightness,
                         const QVector3D &pointLightPosition,
                         float fillBrightness,
                         const QVector3D &fillTint)
{
    const Triangle &triangle = scene.triangle(hit.triangleIndex);
    const QVector3D albedo = triangle.albedo;
    const float roughness = triangle.roughness;

    // A low non-zero ambient term keeps shadowed faces readable. Direct lights
    // still determine shape, surface orientation and occlusion.
    QVector3D radiance = albedo * 0.075f;

    auto applyLight = [&](const QVector3D &lightDirection,
                          const QVector3D &lightColor,
                          float intensity,
                          float shadowDistance) {
        const float diffuse = std::max(0.0f,
            QVector3D::dotProduct(hit.normal, lightDirection));
        if (diffuse <= 0.0f || intensity <= 0.0f)
            return;

        const Ray shadowRay{hit.position + hit.normal * kRayEpsilon, lightDirection};
        if (scene.occluded(shadowRay, shadowDistance))
            return;

        radiance += componentMultiply(albedo, lightColor) * (intensity * diffuse);

        const QVector3D viewDirection = -ray.direction;
        const QVector3D halfDirection = (lightDirection + viewDirection).normalized();
        const float specularPower = 2.0f + (1.0f - roughness) * (1.0f - roughness) * 126.0f;
        const float specular = std::pow(std::max(0.0f,
            QVector3D::dotProduct(hit.normal, halfDirection)), specularPower)
            * (1.0f - roughness) * intensity * 0.12f;
        radiance += componentMultiply(QVector3D(specular, specular, specular), lightColor);
    };

    applyLight(directionalLight, QVector3D(1, 1, 1),
               0.78f * keyBrightness,
               std::numeric_limits<float>::infinity());

    const QVector3D toPointLight = pointLightPosition - hit.position;
    const float distanceSquared = toPointLight.lengthSquared();
    if (distanceSquared > 1.0e-8f) {
        const float distance = std::sqrt(distanceSquared);
        const QVector3D direction = toPointLight / distance;
        const float attenuation = (fillBrightness / 65.0f)
            / (1.0f + 0.00008f * distanceSquared);
        applyLight(direction, fillTint, 0.80f * attenuation,
                   distance - kRayEpsilon);
    }

    return radiance;
}

QVector3D traceRay(const Ray &ray,
                   const SceneAccelerator &scene,
                   const QVector3D &directionalLight,
                   float keyBrightness,
                   const QVector3D &pointLightPosition,
                   float fillBrightness,
                   const QVector3D &fillTint,
                   const QVector3D &background)
{
    Hit hit;
    if (!scene.closestHit(ray, &hit))
        return background;

    return directLighting(ray, hit, scene, directionalLight, keyBrightness,
                          pointLightPosition, fillBrightness, fillTint);
}

QRgb encodePixel(const QVector3D &linearColor)
{
    auto encode = [](float value) {
        // A gentle Reinhard-style tone map keeps bright lights from hard clipping.
        const float mapped = std::max(0.0f, value) / (1.0f + std::max(0.0f, value));
        return static_cast<int>(std::clamp(
            std::lround(linearToSrgb(mapped) * 255.0f), 0L, 255L));
    };
    return qRgba(encode(linearColor.x()),
                 encode(linearColor.y()),
                 encode(linearColor.z()), 255);
}
}

FinalRenderer::FinalRenderer(QObject *parent)
    : QObject(parent)
{
}

FinalRenderer::~FinalRenderer()
{
    cancelRender();
    if (m_worker.joinable())
        m_worker.join();
}

bool FinalRenderer::busy() const
{
    return m_busy.load();
}

double FinalRenderer::progress() const
{
    return m_progress.load();
}

QString FinalRenderer::status() const
{
    std::lock_guard<std::mutex> lock(m_statusMutex);
    return m_status;
}

bool FinalRenderer::startRender(const QUrl &outputUrl,
                                const QVariantMap &scene,
                                int width,
                                int height,
                                int samplesPerPixel)
{
    if (m_busy.load()) {
        setWorkerStatus(QStringLiteral("A render is already running."));
        return false;
    }

    if (m_worker.joinable())
        m_worker.join();

    if (!outputUrl.isLocalFile()) {
        setWorkerStatus(QStringLiteral("Choose a local PNG output path."));
        return false;
    }

    const qint64 pixels = static_cast<qint64>(width) * static_cast<qint64>(height);
    if (width < 16 || height < 16 || width > 16384 || height > 16384
        || pixels > kMaximumRenderPixels) {
        setWorkerStatus(QStringLiteral("Resolution must be at least 16×16 and no larger than 33.5 million pixels."));
        return false;
    }

    if (samplesPerPixel < 1 || samplesPerPixel > 64) {
        setWorkerStatus(QStringLiteral("Samples per pixel must be between 1 and 64."));
        return false;
    }
    if (pixels * samplesPerPixel > 268435456LL) {
        setWorkerStatus(QStringLiteral("That resolution/sample combination exceeds the 268-million-ray budget. Lower the sample count or output resolution."));
        return false;
    }

    m_cancelRequested.store(false);
    m_progress.store(0.0);
    m_busy.store(true);
    emit busyChanged();
    setWorkerStatus(QStringLiteral("Preparing offline render…"));

    const QString outputPath = outputUrl.toLocalFile();
    m_worker = std::thread([this, outputPath, scene, width, height, samplesPerPixel]() {
        runRender(outputPath, scene, width, height, samplesPerPixel);
    });
    return true;
}

void FinalRenderer::cancelRender()
{
    if (m_busy.load()) {
        m_cancelRequested.store(true);
        setWorkerStatus(QStringLiteral("Cancelling render…"));
    }
}

void FinalRenderer::setWorkerProgress(double value)
{
    m_progress.store(std::clamp(value, 0.0, 100.0));
    emit progressChanged();
}

void FinalRenderer::setWorkerStatus(const QString &value)
{
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status = value;
    }
    emit statusChanged();
}

void FinalRenderer::runRender(QString outputPath,
                              QVariantMap scene,
                              int width,
                              int height,
                              int samplesPerPixel)
{
    auto finish = [this](bool success, const QString &message) {
        setWorkerStatus(message);
        m_progress.store(success ? 100.0 : m_progress.load());
        emit progressChanged();
        m_busy.store(false);
        emit busyChanged();
        emit renderFinished(success, message);
    };

    try {
        setWorkerStatus(QStringLiteral("Building scene geometry…"));
        std::vector<Triangle> triangles;
        QString error;
        if (!appendSceneTriangles(scene, &triangles, &error)) {
            finish(false, error);
            return;
        }
        if (m_cancelRequested.load()) {
            finish(false, QStringLiteral("Render cancelled."));
            return;
        }

        setWorkerStatus(QStringLiteral("Building acceleration structure…"));
        SceneAccelerator accelerator(std::move(triangles));
        if (accelerator.triangleCount() <= 0) {
            finish(false, QStringLiteral("The scene has no renderable triangles."));
            return;
        }

        const QVariantMap settings = scene.value(QStringLiteral("settings")).toMap();
        const float yaw = static_cast<float>(numberOr(settings, QStringLiteral("orbitYaw"), 34.0)
                                             * kPi / 180.0);
        const float pitchDegrees = std::clamp(numberOr(settings, QStringLiteral("orbitPitch"), 23.0),
                                              -80.0, 80.0);
        const float pitch = static_cast<float>(pitchDegrees * kPi / 180.0);
        const float distance = static_cast<float>(std::clamp(
            numberOr(settings, QStringLiteral("orbitDistance"), 470.0), 20.0, 1.0e6));
        const float fov = static_cast<float>(std::clamp(
            numberOr(settings, QStringLiteral("cameraFov"), 45.0), 10.0, 120.0));

        const float horizontalDistance = distance * std::cos(pitch);
        const QVector3D cameraPosition(horizontalDistance * std::sin(yaw),
                                       distance * std::sin(pitch),
                                       horizontalDistance * std::cos(yaw));
        const QVector3D forward = (-cameraPosition).normalized();
        QVector3D right = QVector3D::crossProduct(forward, QVector3D(0, 1, 0)).normalized();
        if (right.lengthSquared() < 1.0e-8f)
            right = QVector3D(1, 0, 0);
        const QVector3D up = QVector3D::crossProduct(right, forward).normalized();
        const float imageAspect = static_cast<float>(width) / height;
        const float halfFovScale = std::tan(fov * kPi / 360.0f);
        const QVector3D directionalLight =
            QVector3D(-0.48f, 0.72f, 0.50f).normalized();
        const float keyBrightness = static_cast<float>(std::clamp(
            numberOr(settings, QStringLiteral("keyLightBrightness"), 1.7), 0.0, 4.0));
        const float fillBrightness = static_cast<float>(std::clamp(
            numberOr(settings, QStringLiteral("fillLightBrightness"), 65.0), 0.0, 150.0));
        const QVector3D pointLightPosition(-160.0f, 190.0f, 140.0f);
        const int fillTintIndex = static_cast<int>(std::clamp(
            numberOr(settings, QStringLiteral("fillTintIndex"), 0.0), 0.0, 2.0));
        const QVector3D fillTint = fillTintIndex == 1
            ? colorToLinear(QColor(QStringLiteral("#ffd6ad")))
            : fillTintIndex == 2
                ? colorToLinear(QColor(QStringLiteral("#b8d6ff")))
                : QVector3D(1.0f, 1.0f, 1.0f);
        const QVector3D background = colorToLinear(QColor(QStringLiteral("#171717")));

        QImage image(width, height, QImage::Format_ARGB32);
        if (image.isNull()) {
            finish(false, QStringLiteral("Could not allocate the requested render image."));
            return;
        }

        setWorkerStatus(QStringLiteral("Rendering %1×%2 at %3 sample(s) per pixel…")
                            .arg(width).arg(height).arg(samplesPerPixel));
        std::mt19937 random(0x51414c59U);
        std::uniform_real_distribution<float> jitter(0.0f, 1.0f);

        for (int y = 0; y < height; ++y) {
            if (m_cancelRequested.load()) {
                finish(false, QStringLiteral("Render cancelled. The incomplete image was not saved."));
                return;
            }

            auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < width; ++x) {
                QVector3D accumulated;
                for (int sample = 0; sample < samplesPerPixel; ++sample) {
                    const float sampleX = static_cast<float>(x) + jitter(random);
                    const float sampleY = static_cast<float>(y) + jitter(random);
                    const float imageX = (2.0f * sampleX / width - 1.0f)
                        * imageAspect * halfFovScale;
                    const float imageY = (1.0f - 2.0f * sampleY / height) * halfFovScale;
                    const QVector3D direction =
                        (forward + right * imageX + up * imageY).normalized();
                    const Ray ray{cameraPosition, direction};
                    accumulated += traceRay(ray, accelerator, directionalLight,
                                            keyBrightness, pointLightPosition,
                                            fillBrightness, fillTint, background);
                }

                row[x] = encodePixel(accumulated / static_cast<float>(samplesPerPixel));
            }

            if ((y & 7) == 0 || y == height - 1)
                setWorkerProgress(100.0 * static_cast<double>(y + 1) / height);
        }

        if (m_cancelRequested.load()) {
            finish(false, QStringLiteral("Render cancelled. The incomplete image was not saved."));
            return;
        }

        setWorkerStatus(QStringLiteral("Encoding PNG output…"));
        QByteArray pngBytes;
        QBuffer buffer(&pngBytes);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
            finish(false, QStringLiteral("Could not encode the render as PNG."));
            return;
        }

        QSaveFile output(outputPath);
        if (!output.open(QIODevice::WriteOnly)) {
            finish(false, QStringLiteral("Could not open the render output: %1")
                              .arg(output.errorString()));
            return;
        }
        if (output.write(pngBytes) != pngBytes.size()) {
            output.cancelWriting();
            finish(false, QStringLiteral("Could not write the complete PNG output."));
            return;
        }
        if (!output.commit()) {
            finish(false, QStringLiteral("Could not finish saving the PNG: %1")
                              .arg(output.errorString()));
            return;
        }

        finish(true, QStringLiteral("Render saved: %1 (%2×%3, %4 sample(s)/pixel)")
                         .arg(QFileInfo(outputPath).absoluteFilePath())
                         .arg(width).arg(height).arg(samplesPerPixel));
    } catch (const std::exception &exception) {
        finish(false, QStringLiteral("Render failed: %1")
                          .arg(QString::fromUtf8(exception.what())));
    } catch (...) {
        finish(false, QStringLiteral("Render failed due to an unexpected error."));
    }
}
