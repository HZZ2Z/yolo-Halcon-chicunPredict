#pragma once

#include "types.hpp"

#include <QDateTime>
#include <QList>
#include <QSqlDatabase>
#include <QString>

struct DesktopUser {
    int id = -1;
    QString username;
    QString role;
};

struct StoredMeasurement {
    qint64 id = 0;
    qint64 session_id = 0;
    qint64 frame_id = 0;
    QDateTime timestamp;
    QString username;
    float px = 0.0f;
    float raw_mm = -1.0f;
    float mm = -1.0f;
    float sigma = -1.0f;
    int scans = 0;
    QString quality;
    float cx = 0.0f;
    float cy = 0.0f;
    float angle = 0.0f;
    QString config_version;
    QString calibration_file;
};

struct StoredRunSession {
    qint64 id = 0;
    QDateTime started_at;
    QDateTime ended_at;
    QString username;
    QString status;
    int total_frames = 0;
    int ok_frames = 0;
    int low_quality_frames = 0;
    float mean_px = -1.0f;
    float mean_mm = -1.0f;
    float mean_sigma = -1.0f;
    float mean_scans = -1.0f;
    QString config_version;
    QString calibration_file;
};

class MeasurementDatabase {
public:
    bool open(const QString& path);
    bool hasUsers() const;
    bool createUser(const QString& username, const QString& password, const QString& role);
    bool authenticate(const QString& username, const QString& password, DesktopUser& user) const;

    qint64 startSession(const QString& username,
                        const QString& config_version,
                        const QString& calibration_file);
    bool finishSession(qint64 session_id, const QString& status);
    bool insertMeasurement(const StoredMeasurement& record);
    QList<StoredRunSession> querySessions(const QString& user_filter,
                                          const QString& status_filter,
                                          int limit) const;
    QList<StoredMeasurement> querySessionFrames(qint64 session_id) const;
    bool deleteSessions(const QList<qint64>& session_ids);
    bool deleteAllSessions();
    bool exportSessionsCsv(const QString& path,
                           const QString& user_filter,
                           const QString& status_filter,
                           int limit) const;
    bool exportSessionFramesCsv(const QString& path, qint64 session_id) const;

private:
    static QString makeSalt();
    static QString hashPassword(const QString& salt, const QString& password);

    bool ensureColumn(const QString& table, const QString& column, const QString& definition);
    bool migrateLegacyMeasurements();

    QSqlDatabase db_;
};
