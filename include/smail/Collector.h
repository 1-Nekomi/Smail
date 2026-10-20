#pragma once
#include "smail/Config.h"
#include <QThread>

namespace smail {
class Collector : public QThread {
    Q_OBJECT
public:
    Collector(Config config, QString statePath, QObject *parent = nullptr);
signals:
    void activity(const QString &text);
    void processed(const QString &subject, const QString &detail);
protected:
    void run() override;
private:
    Config config;
    QString statePath;
    void poll();
};
}
