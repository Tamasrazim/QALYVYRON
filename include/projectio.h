#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantMap>

class ProjectIO final : public QObject
{
    Q_OBJECT

public:
    explicit ProjectIO(QObject *parent = nullptr);

    Q_INVOKABLE QVariantMap saveProject(const QUrl &url,
                                       const QVariantMap &document) const;
    Q_INVOKABLE QVariantMap loadProject(const QUrl &url) const;
};
