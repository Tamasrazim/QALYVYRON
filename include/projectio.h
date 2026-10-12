// What this does: Declares the functions for opening and saving QALYVYRON scenes and exchanging geometry with OBJ files.

#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantMap>

class ProjectIO final : public QObject
{
    Q_OBJECT

public:
    explicit ProjectIO(QObject *parent = nullptr);

    Q_INVOKABLE bool showWelcomeOnStartup() const;
    Q_INVOKABLE void setShowWelcomeOnStartup(bool show) const;
    Q_INVOKABLE QVariantMap saveProject(const QUrl &url,
                                       const QVariantMap &document) const;
    Q_INVOKABLE QVariantMap loadProject(const QUrl &url) const;
    Q_INVOKABLE QVariantMap importObj(const QUrl &url) const;
    Q_INVOKABLE QVariantMap exportObj(const QUrl &url,
                                      const QVariantMap &document) const;
};
