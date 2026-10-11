#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantMap>

class ProjectIO final : public QObject
{
    Q_OBJECT

public:
    explicit ProjectIO(QObject *parent = nullptr);

    Q_INVOKABLE bool hasSeenWelcome() const;
    Q_INVOKABLE bool welcomeChoiceMade() const;
    Q_INVOKABLE void setHasSeenWelcome(bool seen) const;
    Q_INVOKABLE QVariantMap saveProject(const QUrl &url,
                                       const QVariantMap &document) const;
    Q_INVOKABLE QVariantMap loadProject(const QUrl &url) const;
    Q_INVOKABLE QVariantMap importObj(const QUrl &url) const;
    Q_INVOKABLE QVariantMap exportObj(const QUrl &url,
                                      const QVariantMap &document) const;
};
