#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantMap>

#include <atomic>
#include <mutex>
#include <thread>

class FinalRenderer final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit FinalRenderer(QObject *parent = nullptr);
    ~FinalRenderer() override;

    bool busy() const;
    double progress() const;
    QString status() const;

    Q_INVOKABLE bool startRender(const QUrl &outputUrl,
                                 const QVariantMap &scene,
                                 int width,
                                 int height,
                                 int samplesPerPixel);
    Q_INVOKABLE void cancelRender();

signals:
    void busyChanged();
    void progressChanged();
    void statusChanged();
    void renderFinished(bool success, const QString &message);

private:
    std::atomic_bool m_busy{false};
    std::atomic_bool m_cancelRequested{false};
    std::atomic<double> m_progress{0.0};
    mutable std::mutex m_statusMutex;
    QString m_status = QStringLiteral("Ready");
    std::thread m_worker;

    void runRender(QString outputPath,
                   QVariantMap scene,
                   int width,
                   int height,
                   int samplesPerPixel);
    void setWorkerProgress(double value);
    void setWorkerStatus(const QString &value);
};
