#include "desktop/measurement_database.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTextStream>
#include <QUuid>
#include <QVariant>

#include <algorithm>

namespace {

bool execSql(QSqlQuery& query, const QString& sql) {
    return query.exec(sql);
}

QString csvEscape(const QString& value) {
    QString escaped = value;
    escaped.replace('"', "\"\"");
    return '"' + escaped + '"';
}

} // namespace

bool MeasurementDatabase::open(const QString& path) {
    const QFileInfo info(path);
    if (!info.absoluteDir().exists()) {
        QDir().mkpath(info.absolutePath());
    }

    db_ = QSqlDatabase::addDatabase("QSQLITE", "metrology_desktop");
    db_.setDatabaseName(path);
    if (!db_.open()) {
        return false;
    }

    QSqlQuery query(db_);
    if (!execSql(query,
                 "CREATE TABLE IF NOT EXISTS users ("
                 "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                 "username TEXT NOT NULL UNIQUE,"
                 "role TEXT NOT NULL,"
                 "salt TEXT NOT NULL,"
                 "password_hash TEXT NOT NULL,"
                 "created_at TEXT NOT NULL)")) {
        return false;
    }

    if (!execSql(query,
                 "CREATE TABLE IF NOT EXISTS measurement_sessions ("
                 "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                 "started_at TEXT NOT NULL,"
                 "ended_at TEXT,"
                 "username TEXT NOT NULL,"
                 "status TEXT NOT NULL,"
                 "total_frames INTEGER NOT NULL DEFAULT 0,"
                 "ok_frames INTEGER NOT NULL DEFAULT 0,"
                 "low_quality_frames INTEGER NOT NULL DEFAULT 0,"
                 "mean_px REAL NOT NULL DEFAULT -1,"
                 "mean_mm REAL NOT NULL DEFAULT -1,"
                 "mean_sigma REAL NOT NULL DEFAULT -1,"
                 "mean_scans REAL NOT NULL DEFAULT -1,"
                 "config_version TEXT NOT NULL,"
                 "calibration_file TEXT NOT NULL)")) {
        return false;
    }

    if (!execSql(query,
                 "CREATE TABLE IF NOT EXISTS measurements ("
                 "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                 "session_id INTEGER,"
                 "frame_id INTEGER NOT NULL DEFAULT 0,"
                 "timestamp TEXT NOT NULL,"
                 "username TEXT NOT NULL,"
                 "px REAL NOT NULL,"
                 "raw_mm REAL NOT NULL,"
                 "mm REAL NOT NULL,"
                 "sigma REAL NOT NULL,"
                 "scans INTEGER NOT NULL,"
                 "quality TEXT NOT NULL,"
                 "cx REAL NOT NULL,"
                 "cy REAL NOT NULL,"
                 "angle REAL NOT NULL,"
                 "config_version TEXT NOT NULL,"
                 "calibration_file TEXT NOT NULL)")) {
        return false;
    }

    return ensureColumn("measurements", "session_id", "session_id INTEGER") &&
           ensureColumn("measurements", "frame_id", "frame_id INTEGER NOT NULL DEFAULT 0") &&
           migrateLegacyMeasurements();
}

bool MeasurementDatabase::hasUsers() const {
    QSqlQuery query(db_);
    if (!query.exec("SELECT COUNT(*) FROM users")) {
        return false;
    }
    return query.next() && query.value(0).toInt() > 0;
}

bool MeasurementDatabase::createUser(const QString& username,
                                     const QString& password,
                                     const QString& role) {
    const QString salt = makeSalt();
    const QString hash = hashPassword(salt, password);
    QSqlQuery query(db_);
    query.prepare("INSERT INTO users(username, role, salt, password_hash, created_at) "
                  "VALUES(?, ?, ?, ?, ?)");
    query.addBindValue(username.trimmed());
    query.addBindValue(role);
    query.addBindValue(salt);
    query.addBindValue(hash);
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    return query.exec();
}

bool MeasurementDatabase::authenticate(const QString& username,
                                       const QString& password,
                                       DesktopUser& user) const {
    QSqlQuery query(db_);
    query.prepare("SELECT id, username, role, salt, password_hash FROM users WHERE username=?");
    query.addBindValue(username.trimmed());
    if (!query.exec() || !query.next()) {
        return false;
    }

    const QString salt = query.value(3).toString();
    const QString expected = query.value(4).toString();
    if (hashPassword(salt, password) != expected) {
        return false;
    }

    user.id = query.value(0).toInt();
    user.username = query.value(1).toString();
    user.role = query.value(2).toString();
    return true;
}

qint64 MeasurementDatabase::startSession(const QString& username,
                                         const QString& config_version,
                                         const QString& calibration_file) {
    QSqlQuery query(db_);
    query.prepare("INSERT INTO measurement_sessions("
                  "started_at, username, status, config_version, calibration_file) "
                  "VALUES(?, ?, ?, ?, ?)");
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    query.addBindValue(username);
    query.addBindValue("RUNNING");
    query.addBindValue(config_version);
    query.addBindValue(calibration_file);
    if (!query.exec()) {
        return -1;
    }
    return query.lastInsertId().toLongLong();
}

bool MeasurementDatabase::finishSession(qint64 session_id, const QString& status) {
    if (session_id <= 0) {
        return false;
    }

    QSqlQuery summary(db_);
    summary.prepare("SELECT COUNT(*), "
                    "SUM(CASE WHEN quality='OK' THEN 1 ELSE 0 END), "
                    "AVG(CASE WHEN quality='OK' THEN px END), "
                    "AVG(CASE WHEN quality='OK' THEN mm END), "
                    "AVG(CASE WHEN quality='OK' THEN sigma END), "
                    "AVG(CASE WHEN quality='OK' THEN scans END) "
                    "FROM measurements WHERE session_id=?");
    summary.addBindValue(session_id);
    if (!summary.exec() || !summary.next()) {
        return false;
    }

    const int total = summary.value(0).toInt();
    const int ok = summary.value(1).toInt();
    const int low = std::max(0, total - ok);
    const double mean_px = summary.value(2).isNull() ? -1.0 : summary.value(2).toDouble();
    const double mean_mm = summary.value(3).isNull() ? -1.0 : summary.value(3).toDouble();
    const double mean_sigma = summary.value(4).isNull() ? -1.0 : summary.value(4).toDouble();
    const double mean_scans = summary.value(5).isNull() ? -1.0 : summary.value(5).toDouble();

    QSqlQuery query(db_);
    query.prepare("UPDATE measurement_sessions SET "
                  "ended_at=?, status=?, total_frames=?, ok_frames=?, low_quality_frames=?, "
                  "mean_px=?, mean_mm=?, mean_sigma=?, mean_scans=? WHERE id=?");
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    query.addBindValue(status);
    query.addBindValue(total);
    query.addBindValue(ok);
    query.addBindValue(low);
    query.addBindValue(mean_px);
    query.addBindValue(mean_mm);
    query.addBindValue(mean_sigma);
    query.addBindValue(mean_scans);
    query.addBindValue(session_id);
    return query.exec();
}

bool MeasurementDatabase::insertMeasurement(const StoredMeasurement& record) {
    QSqlQuery query(db_);
    query.prepare("INSERT INTO measurements("
                  "session_id, frame_id, timestamp, username, px, raw_mm, mm, sigma, scans, quality, "
                  "cx, cy, angle, config_version, calibration_file) "
                  "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    query.addBindValue(record.session_id);
    query.addBindValue(record.frame_id);
    query.addBindValue(record.timestamp.toString(Qt::ISODateWithMs));
    query.addBindValue(record.username);
    query.addBindValue(record.px);
    query.addBindValue(record.raw_mm);
    query.addBindValue(record.mm);
    query.addBindValue(record.sigma);
    query.addBindValue(record.scans);
    query.addBindValue(record.quality);
    query.addBindValue(record.cx);
    query.addBindValue(record.cy);
    query.addBindValue(record.angle);
    query.addBindValue(record.config_version);
    query.addBindValue(record.calibration_file);
    return query.exec();
}

QList<StoredRunSession> MeasurementDatabase::querySessions(const QString& user_filter,
                                                           const QString& status_filter,
                                                           int limit) const {
    QString sql = "SELECT id, started_at, ended_at, username, status, total_frames, ok_frames, "
                  "low_quality_frames, mean_px, mean_mm, mean_sigma, mean_scans, "
                  "config_version, calibration_file FROM measurement_sessions";
    QStringList where;
    if (!user_filter.isEmpty()) {
        where << "username = ?";
    }
    if (!status_filter.isEmpty()) {
        where << "status = ?";
    }
    if (!where.isEmpty()) {
        sql += " WHERE " + where.join(" AND ");
    }
    sql += " ORDER BY id DESC LIMIT ?";

    QSqlQuery query(db_);
    query.prepare(sql);
    if (!user_filter.isEmpty()) {
        query.addBindValue(user_filter);
    }
    if (!status_filter.isEmpty()) {
        query.addBindValue(status_filter);
    }
    query.addBindValue(std::max(1, limit));

    QList<StoredRunSession> rows;
    if (!query.exec()) {
        return rows;
    }

    while (query.next()) {
        StoredRunSession record;
        record.id = query.value(0).toLongLong();
        record.started_at = QDateTime::fromString(query.value(1).toString(), Qt::ISODateWithMs);
        record.ended_at = QDateTime::fromString(query.value(2).toString(), Qt::ISODateWithMs);
        record.username = query.value(3).toString();
        record.status = query.value(4).toString();
        record.total_frames = query.value(5).toInt();
        record.ok_frames = query.value(6).toInt();
        record.low_quality_frames = query.value(7).toInt();
        record.mean_px = query.value(8).toFloat();
        record.mean_mm = query.value(9).toFloat();
        record.mean_sigma = query.value(10).toFloat();
        record.mean_scans = query.value(11).toFloat();
        record.config_version = query.value(12).toString();
        record.calibration_file = query.value(13).toString();
        rows.push_back(record);
    }
    return rows;
}

QList<StoredMeasurement> MeasurementDatabase::querySessionFrames(qint64 session_id) const {
    QSqlQuery query(db_);
    query.prepare("SELECT id, session_id, frame_id, timestamp, username, px, raw_mm, mm, sigma, scans, "
                  "quality, cx, cy, angle, config_version, calibration_file "
                  "FROM measurements WHERE session_id=? ORDER BY frame_id ASC, id ASC");
    query.addBindValue(session_id);

    QList<StoredMeasurement> rows;
    if (!query.exec()) {
        return rows;
    }

    while (query.next()) {
        StoredMeasurement record;
        record.id = query.value(0).toLongLong();
        record.session_id = query.value(1).toLongLong();
        record.frame_id = query.value(2).toLongLong();
        record.timestamp = QDateTime::fromString(query.value(3).toString(), Qt::ISODateWithMs);
        record.username = query.value(4).toString();
        record.px = query.value(5).toFloat();
        record.raw_mm = query.value(6).toFloat();
        record.mm = query.value(7).toFloat();
        record.sigma = query.value(8).toFloat();
        record.scans = query.value(9).toInt();
        record.quality = query.value(10).toString();
        record.cx = query.value(11).toFloat();
        record.cy = query.value(12).toFloat();
        record.angle = query.value(13).toFloat();
        record.config_version = query.value(14).toString();
        record.calibration_file = query.value(15).toString();
        rows.push_back(record);
    }
    return rows;
}

bool MeasurementDatabase::deleteSessions(const QList<qint64>& session_ids) {
    QList<qint64> ids;
    for (const qint64 id : session_ids) {
        if (id > 0 && !ids.contains(id)) {
            ids.push_back(id);
        }
    }
    if (ids.isEmpty()) {
        return true;
    }

    if (!db_.transaction()) {
        return false;
    }

    QSqlQuery delete_frames(db_);
    QSqlQuery delete_sessions(db_);
    delete_frames.prepare("DELETE FROM measurements WHERE session_id=?");
    delete_sessions.prepare("DELETE FROM measurement_sessions WHERE id=?");

    for (const qint64 id : ids) {
        delete_frames.addBindValue(id);
        if (!delete_frames.exec()) {
            db_.rollback();
            return false;
        }
        delete_frames.finish();

        delete_sessions.addBindValue(id);
        if (!delete_sessions.exec()) {
            db_.rollback();
            return false;
        }
        delete_sessions.finish();
    }

    return db_.commit();
}

bool MeasurementDatabase::deleteAllSessions() {
    if (!db_.transaction()) {
        return false;
    }
    QSqlQuery query(db_);
    if (!query.exec("DELETE FROM measurements")) {
        db_.rollback();
        return false;
    }
    if (!query.exec("DELETE FROM measurement_sessions")) {
        db_.rollback();
        return false;
    }
    return db_.commit();
}

QString MeasurementDatabase::makeSalt() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString MeasurementDatabase::hashPassword(const QString& salt, const QString& password) {
    const QByteArray bytes = (salt + ":" + password).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool MeasurementDatabase::exportSessionsCsv(const QString& path,
                                            const QString& user_filter,
                                            const QString& status_filter,
                                            int limit) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&file);
    out << "id,started_at,ended_at,username,status,total_frames,ok_frames,low_quality_frames,"
           "mean_px,mean_mm,mean_sigma,mean_scans,config_version,calibration_file\n";
    const QList<StoredRunSession> rows = querySessions(user_filter, status_filter, limit);
    for (const StoredRunSession& r : rows) {
        out << r.id << ','
            << csvEscape(r.started_at.toString(Qt::ISODateWithMs)) << ','
            << csvEscape(r.ended_at.toString(Qt::ISODateWithMs)) << ','
            << csvEscape(r.username) << ','
            << csvEscape(r.status) << ','
            << r.total_frames << ','
            << r.ok_frames << ','
            << r.low_quality_frames << ','
            << r.mean_px << ','
            << r.mean_mm << ','
            << r.mean_sigma << ','
            << r.mean_scans << ','
            << csvEscape(r.config_version) << ','
            << csvEscape(r.calibration_file) << '\n';
    }
    return true;
}

bool MeasurementDatabase::exportSessionFramesCsv(const QString& path, qint64 session_id) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&file);
    out << "id,session_id,frame_id,timestamp,username,px,raw_mm,mm,sigma,scans,quality,cx,cy,angle,config_version,calibration_file\n";
    const QList<StoredMeasurement> rows = querySessionFrames(session_id);
    for (const StoredMeasurement& r : rows) {
        out << r.id << ','
            << r.session_id << ','
            << r.frame_id << ','
            << csvEscape(r.timestamp.toString(Qt::ISODateWithMs)) << ','
            << csvEscape(r.username) << ','
            << r.px << ','
            << r.raw_mm << ','
            << r.mm << ','
            << r.sigma << ','
            << r.scans << ','
            << csvEscape(r.quality) << ','
            << r.cx << ','
            << r.cy << ','
            << r.angle << ','
            << csvEscape(r.config_version) << ','
            << csvEscape(r.calibration_file) << '\n';
    }
    return true;
}

bool MeasurementDatabase::ensureColumn(const QString& table,
                                       const QString& column,
                                       const QString& definition) {
    QSqlQuery query(db_);
    if (!query.exec("PRAGMA table_info(" + table + ")")) {
        return false;
    }
    while (query.next()) {
        if (query.value(1).toString() == column) {
            return true;
        }
    }

    QSqlQuery alter(db_);
    return alter.exec("ALTER TABLE " + table + " ADD COLUMN " + definition);
}

bool MeasurementDatabase::migrateLegacyMeasurements() {
    QSqlQuery count_query(db_);
    if (!count_query.exec("SELECT COUNT(*) FROM measurements WHERE session_id IS NULL")) {
        return false;
    }
    if (!count_query.next() || count_query.value(0).toInt() <= 0) {
        return true;
    }

    QSqlQuery first(db_);
    if (!first.exec("SELECT username, MIN(timestamp), MAX(timestamp), "
                    "MIN(config_version), MIN(calibration_file) "
                    "FROM measurements WHERE session_id IS NULL")) {
        return false;
    }
    if (!first.next()) {
        return true;
    }

    QSqlQuery insert(db_);
    insert.prepare("INSERT INTO measurement_sessions("
                   "started_at, ended_at, username, status, config_version, calibration_file) "
                   "VALUES(?, ?, ?, ?, ?, ?)");
    insert.addBindValue(first.value(1).toString());
    insert.addBindValue(first.value(2).toString());
    insert.addBindValue(first.value(0).toString());
    insert.addBindValue("LEGACY");
    insert.addBindValue(first.value(3).toString());
    insert.addBindValue(first.value(4).toString());
    if (!insert.exec()) {
        return false;
    }

    const qint64 session_id = insert.lastInsertId().toLongLong();
    QSqlQuery update(db_);
    update.prepare("UPDATE measurements SET session_id=?, frame_id=id WHERE session_id IS NULL");
    update.addBindValue(session_id);
    if (!update.exec()) {
        return false;
    }
    return finishSession(session_id, "LEGACY");
}
