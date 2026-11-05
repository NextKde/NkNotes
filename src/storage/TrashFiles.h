#pragma once
#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QHash>
#include <QJsonDocument>
#include "MarkdownFile.h"
#ifdef Q_OS_LINUX
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace TrashFiles {
// Fingerprint is shown in the review snapshot and checked again before deletion.
inline QString fingerprint(const QString &root, QHash<QString,QString> *manifest = nullptr) {
    QFileInfo info(root); if(info.isSymLink()||!info.exists())return {};
    QStringList files;
    if(info.isDir()) {
        QDirIterator it(root,QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
        while(it.hasNext()){it.next();if(it.fileInfo().isSymLink()||(!it.fileInfo().isFile()&&!it.fileInfo().isDir()))return {};files.append(it.filePath());}
    } else if(info.isFile()) files.append(root); else return {};
    files.sort();QJsonArray rows;
    for(const auto &file:files){QFileInfo fi(file);QString hash="directory";if(fi.isFile()){QFile f(file);if(!f.open(QIODevice::ReadOnly))return {};auto bytes=f.readAll();if(f.error()!=QFileDevice::NoError)return {};hash=MarkdownFile::digest(bytes);}const QString relative=QDir(root).relativeFilePath(file);if(manifest)manifest->insert(relative,hash);rows.append(relative+":"+hash);}
    return MarkdownFile::digest(QJsonDocument(rows).toJson(QJsonDocument::Compact));
}
#ifdef Q_OS_LINUX
// Walk directory descriptors so a concurrent symlink substitution cannot redirect deletion.
inline bool removeContents(int fd, const QHash<QString,QString> &manifest, const QString &prefix = {}) {
    DIR *dir=::fdopendir(::dup(fd));if(!dir)return false;bool ok=true;
    while(auto *entry=::readdir(dir)) {
        QByteArray name(entry->d_name);if(name=="."||name=="..")continue;
        const QString relative=prefix+QString::fromLocal8Bit(name);if(!manifest.contains(relative)){ok=false;break;}
        struct stat st;if(::fstatat(fd,name.constData(),&st,AT_SYMLINK_NOFOLLOW)!=0){ok=false;break;}
        if(S_ISDIR(st.st_mode)){
            int child=::openat(fd,name.constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
            if(child<0){ok=false;break;}bool removed=manifest.value(relative)=="directory"&&removeContents(child,manifest,relative+'/');::close(child);
            if(!removed||::unlinkat(fd,name.constData(),AT_REMOVEDIR)!=0){ok=false;break;}
        } else {
            if(!S_ISREG(st.st_mode)){ok=false;break;}
            int file=::openat(fd,name.constData(),O_RDONLY|O_NOFOLLOW);if(file<0){ok=false;break;}
            QFile content;if(!content.open(file,QIODevice::ReadOnly,QFileDevice::AutoCloseHandle)){::close(file);ok=false;break;}
            const QByteArray bytes=content.readAll();struct stat opened;
            if(content.error()!=QFileDevice::NoError||MarkdownFile::digest(bytes)!=manifest.value(relative)||::fstat(file,&opened)!=0||opened.st_ino!=st.st_ino||opened.st_dev!=st.st_dev||::unlinkat(fd,name.constData(),0)!=0){ok=false;break;}
        }
    }
    ::closedir(dir);return ok;
}
#endif
inline bool remove(const QString &path,bool folder,const QString &expected) {
#ifdef Q_OS_LINUX
    QHash<QString,QString> manifest;if(fingerprint(path,&manifest)!=expected)return false;
    if(!folder)return QFile::remove(path);
    int fd=::open(QFile::encodeName(path).constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(fd<0)return false;
    bool ok=removeContents(fd,manifest);::close(fd);return ok&&::rmdir(QFile::encodeName(path).constData())==0;
#else
    Q_UNUSED(path);Q_UNUSED(folder);Q_UNUSED(expected);return false;
#endif
}
}
