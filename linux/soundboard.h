#pragma once
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

class Soundboard {
public:
  QMap<QString, QString> entries;
  QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QString error;
  static QString command(QString name) {
    name = name.trimmed().toLower();
    if (name.startsWith('/')) name.remove(0, 1);
    name.replace(QRegularExpression("[^\\p{L}\\p{N}_-]+"), "-");
    name.remove(QRegularExpression("^-+|-+$"));
    return name;
  }
  QString unique(QString name) const {
    name = command(name);
    if (name.isEmpty()) name = "sound";
    if (name == "stop") name = "sound-stop";
    QString result = name;
    for (int i = 2; entries.contains(result); ++i) result = name + "-" + QString::number(i);
    return result;
  }
  bool load() {
    QFile file(root + "/soundboard.json");
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isArray()) {
      error = "Invalid soundboard library"; return false;
    }
    entries.clear();
    for (auto value : doc.array()) {
      auto item = value.toObject();
      entries[item["command"].toString()] = item["file"].toString();
    }
    return true;
  }
  bool save() {
    QDir().mkpath(root);
    QJsonArray array;
    for (auto it = entries.cbegin(); it != entries.cend(); ++it)
      array.append(QJsonObject{{"command", it.key()}, {"file", it.value()}});
    QSaveFile file(root + "/soundboard.json");
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(array).toJson()) < 0 || !file.commit()) {
      error = file.errorString(); return false;
    }
    return true;
  }
  bool add(const QString &path) {
    QFileInfo info(path);
    if (!info.isFile()) { error = "Missing audio: " + path; return false; }
    QDir().mkpath(root + "/sounds");
    auto hash = QString::fromLatin1(QCryptographicHash::hash(info.canonicalFilePath().toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
    auto target = root + "/sounds/" + hash + "-" + info.fileName();
    for (auto it = entries.cbegin(); it != entries.cend(); ++it)
      if (it.value() == target) return true;
    if (!QFile::exists(target) && !QFile::copy(path, target)) {
      error = "Could not copy " + path; return false;
    }
    entries[unique(info.completeBaseName())] = target;
    return save();
  }
};
