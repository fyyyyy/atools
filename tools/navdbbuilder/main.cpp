/*****************************************************************************
* Copyright 2015-2026 Alexander Barthel alex@littlenavmap.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "exception.h"
#include "fs/db/databasemeta.h"
#include "fs/fspaths.h"
#include "fs/navdatabase.h"
#include "fs/navdatabaseerrors.h"
#include "fs/navdatabaseflags.h"
#include "fs/navdatabaseoptions.h"
#include "fs/navdatabaseprogress.h"
#include "sql/sqldatabase.h"
#include "win/activationcontext.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringBuilder>
#include <QStringList>
#include <QTextStream>

namespace {

const QLatin1String DATABASE_CONNECTION("NAVDBBUILDER");
bool pauseOnExit = true;

QString cleanPath(const QString& path)
{
  if(path.isEmpty())
    return QString();
  return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

void openDatabaseFile(atools::sql::SqlDatabase& db, const QString& file)
{
  QStringList pragmas({
    QStringLiteral("PRAGMA cache_size=-50000"),
    QStringLiteral("PRAGMA page_size=8196"),
    QStringLiteral("PRAGMA locking_mode=EXCLUSIVE"),
    QStringLiteral("PRAGMA journal_mode=TRUNCATE"),
    QStringLiteral("PRAGMA synchronous=OFF"),
    QStringLiteral("PRAGMA busy_timeout=2000"),
    QStringLiteral("PRAGMA foreign_keys = OFF")
  });

  db.setDatabaseName(file);
  db.setAutocommit(false);
  db.setAutomaticTransactions(true);
  db.open(pragmas, false /* readonly */);
}

void createEmptySchema(atools::sql::SqlDatabase& db)
{
  atools::fs::NavDatabaseOptions opts;
  atools::fs::NavDatabase(opts, db, nullptr, QStringLiteral(GIT_REVISION_ATOOLS)).createSchema();
  atools::fs::db::DatabaseMeta(&db).updateVersion();
}

void applyLittleNavmapDefaults(atools::fs::NavDatabaseOptions& opts)
{
  opts.setReadInactive(false);
  opts.setVerbose(false);
  opts.setResolveAirways(true);
  opts.setLanguage(QStringLiteral("en-US"));
  opts.setDatabaseReport(true);
  opts.setDeletes(true);
  opts.setDeduplicate(true);
  opts.setFilterOutDummyRunways(true);
  opts.setWriteIncompleteObjects(true);
  opts.setAutocommit(false);
  opts.setFlag(atools::fs::type::BASIC_VALIDATION, false);
  opts.setFlag(atools::fs::type::AIRPORT_VALIDATION, false);
  opts.setFlag(atools::fs::type::VACUUM_DATABASE, true);
  opts.setFlag(atools::fs::type::ANALYZE_DATABASE, true);
  opts.setFlag(atools::fs::type::DROP_INDEXES, false);
  opts.setFlag(atools::fs::type::DROP_TEMP_TABLES, true);
  opts.setSimConnectAirportFetchDelay(100);
  opts.setSimConnectNavaidFetchDelay(50);
  opts.setSimConnectBatchSize(2000);
  opts.setSimConnectLoadDisconnected(true);
  opts.setSimConnectLoadDisconnectedFile(true);
  opts.excludeNavDbObjectTypes({QStringLiteral("TAXIWAYRUNWAY")});
}

bool progressCallback(const atools::fs::NavDatabaseProgress& progress)
{
  static int lastPrinted = -1;
  QTextStream out(stdout);

  if(progress.isFirstCall())
    out << "Starting database compilation" << Qt::endl;

  if(progress.isNewOther())
    out << progress.getCurrent() << "/" << progress.getTotal() << " " << progress.getOtherAction() << Qt::endl;
  else if(progress.isNewSceneryArea())
    out << progress.getCurrent() << "/" << progress.getTotal() << " Scenery: " << progress.getSceneryTitle() << Qt::endl;
  else if(progress.isNewFile() && progress.getCurrent() != lastPrinted)
  {
    out << progress.getCurrent() << "/" << progress.getTotal() << " File: " << progress.getFileName() << Qt::endl;
    lastPrinted = progress.getCurrent();
  }

  if(progress.isLastCall())
  {
    out << "Done. Files: " << progress.getNumFiles()
        << ", airports: " << progress.getNumAirports()
        << ", VOR: " << progress.getNumVors()
        << ", ILS: " << progress.getNumIls()
        << ", NDB: " << progress.getNumNdbs()
        << ", waypoints: " << progress.getNumWaypoints()
        << ", errors: " << progress.getNumErrors() << Qt::endl;
  }

  return false;
}

void addPathValues(const QStringList& values, void (atools::fs::NavDatabaseOptions::*setter)(const QFileInfo&),
                   atools::fs::NavDatabaseOptions& opts)
{
  for(const QString& value : values)
    (opts.*setter)(QFileInfo(cleanPath(value)));
}

QString defaultTempFile(const QString& outputFile)
{
  const QFileInfo outputInfo(outputFile);
  return outputInfo.absoluteDir().absoluteFilePath(outputInfo.completeBaseName() % QStringLiteral("_compiling.sqlite"));
}

void waitForExit()
{
  if(!pauseOnExit)
    return;

  QTextStream(stdout) << "Press Enter to exit..." << Qt::endl;
  QTextStream(stdin).readLine();
}

int finish(int exitCode)
{
  waitForExit();
  return exitCode;
}

int fail(const QString& message)
{
  QTextStream(stderr) << "navdbbuilder: " << message << Qt::endl;
  return finish(1);
}

} // namespace

int main(int argc, char *argv[])
{
  QCoreApplication app(argc, argv);
  Q_INIT_RESOURCE(atools);
  Q_INIT_RESOURCE(navdata);

  QCoreApplication::setApplicationName(QStringLiteral("navdbbuilder"));
  QCoreApplication::setApplicationVersion(QStringLiteral(VERSION_NUMBER_ATOOLS));

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("Build an atools/Little Navmap SQLite scenery database."));
  parser.addHelpOption();
  parser.addVersionOption();

  const QCommandLineOption outputOpt({QStringLiteral("o"), QStringLiteral("output")},
                                     QStringLiteral("Final SQLite database filename."), QStringLiteral("file"));
  const QCommandLineOption tempOpt(QStringLiteral("temp"),
                                   QStringLiteral("Temporary compile database filename. Defaults next to output."),
                                   QStringLiteral("file"));
  const QCommandLineOption simulatorOpt({QStringLiteral("s"), QStringLiteral("simulator")},
                                        QStringLiteral("Simulator type, for example MSFS24, MSFS, XP12, NAVIGRAPH."),
                                        QStringLiteral("type"), QStringLiteral("MSFS24"));
  const QCommandLineOption basePathOpt({QStringLiteral("b"), QStringLiteral("basepath")},
                                       QStringLiteral("Simulator base path."), QStringLiteral("path"));
  const QCommandLineOption sceneryFileOpt(QStringLiteral("scenery-file"),
                                          QStringLiteral("Scenery.cfg or equivalent simulator scenery file."),
                                          QStringLiteral("file"));
  const QCommandLineOption configOpt({QStringLiteral("c"), QStringLiteral("config")},
                                     QStringLiteral("NavDatabaseOptions .ini/.cfg file."), QStringLiteral("file"));
  const QCommandLineOption timezoneOpt(QStringLiteral("timezone-db"),
                                       QStringLiteral("Timezone database path."), QStringLiteral("file"));
  const QCommandLineOption sourceDbOpt(QStringLiteral("source-db"),
                                       QStringLiteral("Source database for Navigraph/DFD builds."), QStringLiteral("file"));
  const QCommandLineOption msfsCommunityOpt(QStringLiteral("msfs-community"),
                                            QStringLiteral("MSFS Community path."), QStringLiteral("path"));
  const QCommandLineOption msfsOfficialOpt(QStringLiteral("msfs-official"),
                                           QStringLiteral("MSFS Official/OneStore or Official/Steam path."),
                                           QStringLiteral("path"));
  const QCommandLineOption includeOpt(QStringLiteral("include"),
                                      QStringLiteral("Extra include directory. Can be passed more than once."),
                                      QStringLiteral("path"));
  const QCommandLineOption excludeOpt(QStringLiteral("exclude"),
                                      QStringLiteral("Directory or file to exclude. Can be passed more than once."),
                                      QStringLiteral("path"));
  const QCommandLineOption addonExcludeOpt(QStringLiteral("addon-exclude"),
                                           QStringLiteral("Directory to exclude from add-on detection. Can be passed more than once."),
                                           QStringLiteral("path"));
  const QCommandLineOption simconnectDllOpt(QStringLiteral("simconnect-dll"),
                                            QStringLiteral("MSFS 2024 SimConnect loader DLL path or name."),
                                            QStringLiteral("file"));
  const QCommandLineOption simconnectManifestOpt(QStringLiteral("simconnect-manifest"),
                                                  QStringLiteral("Optional Windows activation context manifest."),
                                                  QStringLiteral("file"));
  const QCommandLineOption languageOpt(QStringLiteral("language"),
                                        QStringLiteral("MSFS language, for example en-US."), QStringLiteral("locale"));
  const QCommandLineOption forceOpt({QStringLiteral("f"), QStringLiteral("force")},
                                    QStringLiteral("Overwrite the final output database if it exists."));
  const QCommandLineOption schemaOnlyOpt(QStringLiteral("schema-only"),
                                         QStringLiteral("Create only an empty schema."));
  const QCommandLineOption readInactiveOpt(QStringLiteral("read-inactive"),
                                            QStringLiteral("Read inactive scenery entries."));
  const QCommandLineOption readAddonXmlOpt(QStringLiteral("read-addon-xml"),
                                            QStringLiteral("Read add-on.xml entries."));
  const QCommandLineOption noPauseOpt(QStringLiteral("no-pause"),
                                      QStringLiteral("Exit immediately without waiting for Enter."));

  parser.addOptions({outputOpt, tempOpt, simulatorOpt, basePathOpt, sceneryFileOpt, configOpt, timezoneOpt, sourceDbOpt,
                      msfsCommunityOpt, msfsOfficialOpt, includeOpt, excludeOpt, addonExcludeOpt, simconnectDllOpt,
                      simconnectManifestOpt, languageOpt, forceOpt, schemaOnlyOpt, readInactiveOpt, readAddonXmlOpt,
                      noPauseOpt});
  parser.process(app);
  pauseOnExit = !parser.isSet(noPauseOpt);

  const QString outputFile = cleanPath(parser.value(outputOpt));
  if(outputFile.isEmpty())
    return fail(QStringLiteral("--output is required."));

  const QString tempFile = parser.isSet(tempOpt) ? cleanPath(parser.value(tempOpt)) : defaultTempFile(outputFile);
  if(tempFile == outputFile)
    return fail(QStringLiteral("--temp must be different from --output."));

  const atools::fs::FsPaths::SimulatorType simulatorType =
    atools::fs::FsPaths::stringToType(parser.value(simulatorOpt));
  if(simulatorType == atools::fs::FsPaths::NONE)
    return fail(QStringLiteral("Unknown simulator type \"%1\".").arg(parser.value(simulatorOpt)));

  if(QFileInfo::exists(outputFile) && !parser.isSet(forceOpt))
    return fail(QStringLiteral("Output file exists. Use --force to replace it: %1").arg(outputFile));

  const QFileInfo outputInfo(outputFile);
  if(!QDir().mkpath(outputInfo.absolutePath()))
    return fail(QStringLiteral("Cannot create output directory: %1").arg(outputInfo.absolutePath()));

  QFile::remove(tempFile);
  QFile::remove(tempFile % QStringLiteral("-journal"));

  try
  {
    atools::sql::SqlDatabase::addDatabase(QStringLiteral("QSQLITE"), DATABASE_CONNECTION);
    atools::sql::SqlDatabase db(DATABASE_CONNECTION);
    openDatabaseFile(db, tempFile);

    atools::fs::NavDatabaseOptions opts;
    applyLittleNavmapDefaults(opts);
    opts.setSimulatorType(simulatorType);

    if(parser.isSet(configOpt))
    {
      QSettings settings(cleanPath(parser.value(configOpt)), QSettings::IniFormat);
      opts.loadFromSettings(settings);
      opts.setSimulatorType(simulatorType);
    }

    if(parser.isSet(basePathOpt))
      opts.setBasepath(cleanPath(parser.value(basePathOpt)));
    if(parser.isSet(sceneryFileOpt))
      opts.setSceneryFile(cleanPath(parser.value(sceneryFileOpt)));
    if(parser.isSet(sourceDbOpt))
      opts.setSourceDatabase(cleanPath(parser.value(sourceDbOpt)));
    if(parser.isSet(msfsCommunityOpt))
      opts.setMsfsCommunityPath(cleanPath(parser.value(msfsCommunityOpt)));
    if(parser.isSet(msfsOfficialOpt))
      opts.setMsfsOfficialPath(cleanPath(parser.value(msfsOfficialOpt)));
    if(parser.isSet(timezoneOpt))
      opts.setTimeZoneDatabase(cleanPath(parser.value(timezoneOpt)));
    if(parser.isSet(languageOpt))
      opts.setLanguage(parser.value(languageOpt));

    opts.setReadInactive(parser.isSet(readInactiveOpt) || opts.isReadInactive());
    opts.setReadAddOnXml(parser.isSet(readAddonXmlOpt) || opts.isReadAddOnXml());
    opts.setProgressCallback(progressCallback);
    opts.setCallDefaultCallback(true);

    addPathValues(parser.values(includeOpt), &atools::fs::NavDatabaseOptions::addIncludeGui, opts);
    addPathValues(parser.values(excludeOpt), &atools::fs::NavDatabaseOptions::addExcludeGui, opts);
    addPathValues(parser.values(addonExcludeOpt), &atools::fs::NavDatabaseOptions::addAddonExcludeGui, opts);

    atools::fs::NavDatabaseErrors errors;
    atools::fs::NavDatabase navDatabase(opts, db, &errors, QStringLiteral(GIT_REVISION_ATOOLS));

    atools::win::ActivationContext activationContext;
    QString simconnectLibraryName;
    if(!parser.isSet(schemaOnlyOpt) && simulatorType == atools::fs::FsPaths::MSFS_2024)
    {
      const QString simconnectDll = cleanPath(parser.value(simconnectDllOpt));
      if(simconnectDll.isEmpty())
        return fail(QStringLiteral("MSFS 2024 builds require --simconnect-dll."));

      if(parser.isSet(simconnectManifestOpt))
      {
        const QString manifest = cleanPath(parser.value(simconnectManifestOpt));
        if(!activationContext.create(manifest) || !activationContext.activate())
          return fail(QStringLiteral("Cannot create or activate manifest: %1").arg(manifest));
      }

      if(!activationContext.loadLibrary(simconnectDll))
        return fail(QStringLiteral("Cannot load SimConnect DLL: %1").arg(simconnectDll));

      simconnectLibraryName = QFileInfo(simconnectDll).fileName();
      navDatabase.setActivationContext(&activationContext, simconnectLibraryName);
    }

    if(parser.isSet(schemaOnlyOpt))
    {
      createEmptySchema(db);
      db.commit();
    }
    else
    {
      createEmptySchema(db);
      const atools::fs::ResultFlags result = navDatabase.compileDatabase();
      if(result.testFlag(atools::fs::COMPILE_CANCELED) || result.testFlag(atools::fs::COMPILE_FAILED))
        return fail(QStringLiteral("Database compilation did not complete. Result flags: %1").arg(result.asFlagType()));
    }

    db.close();
    if(!simconnectLibraryName.isEmpty())
      activationContext.freeLibrary(simconnectLibraryName);

    if(QFileInfo::exists(outputFile) && !QFile::remove(outputFile))
      return fail(QStringLiteral("Cannot replace output file: %1").arg(outputFile));

    if(!QFile::rename(tempFile, outputFile))
      return fail(QStringLiteral("Cannot rename temporary database \"%1\" to \"%2\".").arg(tempFile, outputFile));

    QTextStream(stdout) << "Created " << outputFile << Qt::endl;
    atools::sql::SqlDatabase::removeDatabase(DATABASE_CONNECTION);
  }
  catch(const std::exception& e)
  {
    return fail(QString::fromLocal8Bit(e.what()));
  }
  catch(...)
  {
    return fail(QStringLiteral("Unknown exception."));
  }

  return finish(0);
}
