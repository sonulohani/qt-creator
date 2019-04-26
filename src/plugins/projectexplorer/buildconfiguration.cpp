/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "buildconfiguration.h"

#include "buildenvironmentwidget.h"
#include "buildinfo.h"
#include "buildsteplist.h"
#include "namedwidget.h"
#include "kit.h"
#include "kitinformation.h"
#include "kitmanager.h"
#include "project.h"
#include "projectconfigurationaspects.h"
#include "projectexplorer.h"
#include "projectexplorerconstants.h"
#include "projectmacroexpander.h"
#include "projecttree.h"
#include "target.h"

#include <coreplugin/idocument.h>

#include <utils/algorithm.h>
#include <utils/detailswidget.h>
#include <utils/macroexpander.h>
#include <utils/mimetypes/mimedatabase.h>
#include <utils/mimetypes/mimetype.h>
#include <utils/qtcassert.h>

#include <QDebug>
#include <QFormLayout>

static const char BUILD_STEP_LIST_COUNT[] = "ProjectExplorer.BuildConfiguration.BuildStepListCount";
static const char BUILD_STEP_LIST_PREFIX[] = "ProjectExplorer.BuildConfiguration.BuildStepList.";
static const char CLEAR_SYSTEM_ENVIRONMENT_KEY[] = "ProjectExplorer.BuildConfiguration.ClearSystemEnvironment";
static const char USER_ENVIRONMENT_CHANGES_KEY[] = "ProjectExplorer.BuildConfiguration.UserEnvironmentChanges";
static const char BUILDDIRECTORY_KEY[] = "ProjectExplorer.BuildConfiguration.BuildDirectory";

namespace ProjectExplorer {

BuildConfiguration::BuildConfiguration(Target *target, Core::Id id)
    : ProjectConfiguration(target, id)
{
    Utils::MacroExpander *expander = macroExpander();
    expander->setDisplayName(tr("Build Settings"));
    expander->setAccumulating(true);
    expander->registerSubProvider([target] { return target->macroExpander(); });

    expander->registerVariable("buildDir", tr("Build directory"),
            [this] { return buildDirectory().toUserOutput(); });

    expander->registerVariable(Constants::VAR_CURRENTBUILD_NAME, tr("Name of current build"),
            [this] { return displayName(); }, false);

    expander->registerPrefix(Constants::VAR_CURRENTBUILD_ENV,
                             tr("Variables in the current build environment"),
                             [this](const QString &var) { return environment().value(var); });

    updateCacheAndEmitEnvironmentChanged();
    connect(target, &Target::kitChanged,
            this, &BuildConfiguration::updateCacheAndEmitEnvironmentChanged);
    connect(this, &BuildConfiguration::environmentChanged,
            this, &BuildConfiguration::emitBuildDirectoryChanged);
    // Many macroexpanders are based on the current project, so they may change the environment:
    connect(ProjectTree::instance(), &ProjectTree::currentProjectChanged,
            this, &BuildConfiguration::updateCacheAndEmitEnvironmentChanged);

    m_buildDirectoryAspect = addAspect<BaseStringAspect>();
    m_buildDirectoryAspect->setSettingsKey(BUILDDIRECTORY_KEY);
    m_buildDirectoryAspect->setLabelText(tr("Build directory:"));
    m_buildDirectoryAspect->setDisplayStyle(BaseStringAspect::PathChooserDisplay);
    m_buildDirectoryAspect->setExpectedKind(Utils::PathChooser::Directory);
    m_buildDirectoryAspect->setBaseFileName(target->project()->projectDirectory());
    m_buildDirectoryAspect->setEnvironment(environment());
    connect(m_buildDirectoryAspect, &BaseStringAspect::changed,
            this, &BuildConfiguration::buildDirectoryChanged);

    connect(this, &BuildConfiguration::environmentChanged, this, [this] {
        m_buildDirectoryAspect->setEnvironment(environment());
    });
}

Utils::FileName BuildConfiguration::buildDirectory() const
{
    QString path = environment().expandVariables(m_buildDirectoryAspect->value());
    path = QDir::cleanPath(macroExpander()->expand(path));
    return Utils::FileName::fromString(QDir::cleanPath(QDir(target()->project()->projectDirectory().toString()).absoluteFilePath(path)));
}

Utils::FileName BuildConfiguration::rawBuildDirectory() const
{
    return m_buildDirectoryAspect->fileName();
}

void BuildConfiguration::setBuildDirectory(const Utils::FileName &dir)
{
    if (dir == m_buildDirectoryAspect->fileName())
        return;
    m_buildDirectoryAspect->setFileName(dir);
    emitBuildDirectoryChanged();
}

NamedWidget *BuildConfiguration::createConfigWidget()
{
    NamedWidget *named = new NamedWidget;
    named->setDisplayName(m_configWidgetDisplayName);

    QWidget *widget = nullptr;

    if (m_configWidgetHasFrame) {
        auto container = new Utils::DetailsWidget(named);
        widget = new QWidget(container);
        container->setState(Utils::DetailsWidget::NoSummary);
        container->setWidget(widget);

        auto vbox = new QVBoxLayout(named);
        vbox->setMargin(0);
        vbox->addWidget(container);
    } else {
        widget = named;
    }

    auto formLayout = new QFormLayout(widget);
    formLayout->setMargin(0);
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    for (ProjectConfigurationAspect *aspect : aspects()) {
        if (aspect->isVisible())
            aspect->addToConfigurationLayout(formLayout);
    }

    return named;
}

void BuildConfiguration::initialize(const BuildInfo &info)
{
    setDisplayName(info.displayName);
    setDefaultDisplayName(info.displayName);
    setBuildDirectory(info.buildDirectory);

    m_stepLists.append(new BuildStepList(this, Constants::BUILDSTEPS_BUILD));
    m_stepLists.append(new BuildStepList(this, Constants::BUILDSTEPS_CLEAN));
}

QList<NamedWidget *> BuildConfiguration::createSubConfigWidgets()
{
    return {new BuildEnvironmentWidget(this)};
}

QList<Core::Id> BuildConfiguration::knownStepLists() const
{
    return Utils::transform(m_stepLists, &BuildStepList::id);
}

BuildStepList *BuildConfiguration::stepList(Core::Id id) const
{
    return Utils::findOrDefault(m_stepLists, Utils::equal(&BuildStepList::id, id));
}

QVariantMap BuildConfiguration::toMap() const
{
    QVariantMap map(ProjectConfiguration::toMap());
    map.insert(QLatin1String(CLEAR_SYSTEM_ENVIRONMENT_KEY), m_clearSystemEnvironment);
    map.insert(QLatin1String(USER_ENVIRONMENT_CHANGES_KEY), Utils::EnvironmentItem::toStringList(m_userEnvironmentChanges));

    map.insert(QLatin1String(BUILD_STEP_LIST_COUNT), m_stepLists.count());
    for (int i = 0; i < m_stepLists.count(); ++i)
        map.insert(QLatin1String(BUILD_STEP_LIST_PREFIX) + QString::number(i), m_stepLists.at(i)->toMap());

    return map;
}

bool BuildConfiguration::fromMap(const QVariantMap &map)
{
    m_clearSystemEnvironment = map.value(QLatin1String(CLEAR_SYSTEM_ENVIRONMENT_KEY)).toBool();
    m_userEnvironmentChanges = Utils::EnvironmentItem::fromStringList(map.value(QLatin1String(USER_ENVIRONMENT_CHANGES_KEY)).toStringList());

    updateCacheAndEmitEnvironmentChanged();

    qDeleteAll(m_stepLists);
    m_stepLists.clear();

    int maxI = map.value(QLatin1String(BUILD_STEP_LIST_COUNT), 0).toInt();
    for (int i = 0; i < maxI; ++i) {
        QVariantMap data = map.value(QLatin1String(BUILD_STEP_LIST_PREFIX) + QString::number(i)).toMap();
        if (data.isEmpty()) {
            qWarning() << "No data for build step list" << i << "found!";
            continue;
        }
        auto list = new BuildStepList(this, idFromMap(data));
        if (!list->fromMap(data)) {
            qWarning() << "Failed to restore build step list" << i;
            delete list;
            return false;
        }
        m_stepLists.append(list);
    }

    // We currently assume there to be at least a clean and build list!
    QTC_CHECK(knownStepLists().contains(Core::Id(Constants::BUILDSTEPS_BUILD)));
    QTC_CHECK(knownStepLists().contains(Core::Id(Constants::BUILDSTEPS_CLEAN)));

    return ProjectConfiguration::fromMap(map);
}

void BuildConfiguration::updateCacheAndEmitEnvironmentChanged()
{
    Utils::Environment env = baseEnvironment();
    env.modify(userEnvironmentChanges());
    if (env == m_cachedEnvironment)
        return;
    m_cachedEnvironment = env;
    emit environmentChanged(); // might trigger buildDirectoryChanged signal!
}

void BuildConfiguration::emitBuildDirectoryChanged()
{
    if (buildDirectory() != m_lastEmmitedBuildDirectory) {
        m_lastEmmitedBuildDirectory = buildDirectory();
        emit buildDirectoryChanged();
    }
}

ProjectExplorer::BaseStringAspect *BuildConfiguration::buildDirectoryAspect() const
{
    return m_buildDirectoryAspect;
}

void BuildConfiguration::setConfigWidgetDisplayName(const QString &display)
{
    m_configWidgetDisplayName = display;
}

void BuildConfiguration::setBuildDirectoryHistoryCompleter(const QString &history)
{
    m_buildDirectoryAspect->setHistoryCompleter(history);
}

void BuildConfiguration::setConfigWidgetHasFrame(bool configWidgetHasFrame)
{
    m_configWidgetHasFrame = configWidgetHasFrame;
}

void BuildConfiguration::setBuildDirectorySettingsKey(const QString &key)
{
    m_buildDirectoryAspect->setSettingsKey(key);
}

Target *BuildConfiguration::target() const
{
    return static_cast<Target *>(parent());
}

Project *BuildConfiguration::project() const
{
    return target()->project();
}

Utils::Environment BuildConfiguration::baseEnvironment() const
{
    Utils::Environment result;
    if (useSystemEnvironment())
        result = Utils::Environment::systemEnvironment();
    addToEnvironment(result);
    target()->kit()->addToEnvironment(result);
    return result;
}

QString BuildConfiguration::baseEnvironmentText() const
{
    if (useSystemEnvironment())
        return tr("System Environment");
    else
        return tr("Clean Environment");
}

Utils::Environment BuildConfiguration::environment() const
{
    return m_cachedEnvironment;
}

void BuildConfiguration::setUseSystemEnvironment(bool b)
{
    if (useSystemEnvironment() == b)
        return;
    m_clearSystemEnvironment = !b;
    updateCacheAndEmitEnvironmentChanged();
}

void BuildConfiguration::addToEnvironment(Utils::Environment &env) const
{
    Q_UNUSED(env);
}

bool BuildConfiguration::useSystemEnvironment() const
{
    return !m_clearSystemEnvironment;
}

QList<Utils::EnvironmentItem> BuildConfiguration::userEnvironmentChanges() const
{
    return m_userEnvironmentChanges;
}

void BuildConfiguration::setUserEnvironmentChanges(const QList<Utils::EnvironmentItem> &diff)
{
    if (m_userEnvironmentChanges == diff)
        return;
    m_userEnvironmentChanges = diff;
    updateCacheAndEmitEnvironmentChanged();
}

bool BuildConfiguration::isEnabled() const
{
    return true;
}

QString BuildConfiguration::disabledReason() const
{
    return QString();
}

bool BuildConfiguration::regenerateBuildFiles(Node *node)
{
    Q_UNUSED(node);
    return false;
}

QString BuildConfiguration::buildTypeName(BuildConfiguration::BuildType type)
{
    switch (type) {
    case ProjectExplorer::BuildConfiguration::Debug:
        return QLatin1String("debug");
    case ProjectExplorer::BuildConfiguration::Profile:
        return QLatin1String("profile");
    case ProjectExplorer::BuildConfiguration::Release:
        return QLatin1String("release");
    case ProjectExplorer::BuildConfiguration::Unknown: // fallthrough
    default:
        return QLatin1String("unknown");
    }
}

bool BuildConfiguration::isActive() const
{
    return target()->isActive() && target()->activeBuildConfiguration() == this;
}

/*!
 * Helper function that prepends the directory containing the C++ toolchain to
 * PATH. This is used to in build configurations targeting broken build systems
 * to provide hints about which compiler to use.
 */
void BuildConfiguration::prependCompilerPathToEnvironment(Utils::Environment &env) const
{
    return prependCompilerPathToEnvironment(target()->kit(), env);
}

void BuildConfiguration::prependCompilerPathToEnvironment(Kit *k, Utils::Environment &env)
{
    const ToolChain *tc
            = ToolChainKitAspect::toolChain(k, ProjectExplorer::Constants::CXX_LANGUAGE_ID);

    if (!tc)
        return;

    const Utils::FileName compilerDir = tc->compilerCommand().parentDir();
    if (!compilerDir.isEmpty())
        env.prependOrSetPath(compilerDir.toString());
}

///
// IBuildConfigurationFactory
///

static QList<BuildConfigurationFactory *> g_buildConfigurationFactories;

BuildConfigurationFactory::BuildConfigurationFactory()
{
    // Note: Order matters as first-in-queue wins.
    g_buildConfigurationFactories.prepend(this);
}

BuildConfigurationFactory::~BuildConfigurationFactory()
{
    g_buildConfigurationFactories.removeOne(this);
}

const QList<Task> BuildConfigurationFactory::reportIssues(ProjectExplorer::Kit *kit, const QString &projectPath,
                                                          const QString &buildDir) const
{
    if (m_issueReporter)
        return m_issueReporter(kit, projectPath, buildDir);
    return {};
}

const QList<BuildInfo> BuildConfigurationFactory::allAvailableBuilds(const Target *parent) const
{
    return availableBuilds(parent);
}

const QList<BuildInfo> BuildConfigurationFactory::allAvailableSetups(const Kit *k, const QString &projectPath) const
{
    return availableSetups(k, projectPath);
}

bool BuildConfigurationFactory::supportsTargetDeviceType(Core::Id id) const
{
    if (m_supportedTargetDeviceTypes.isEmpty())
        return true;
    return m_supportedTargetDeviceTypes.contains(id);
}

// setup
BuildConfigurationFactory *BuildConfigurationFactory::find(const Kit *k, const QString &projectPath)
{
    QTC_ASSERT(k, return nullptr);
    const Core::Id deviceType = DeviceTypeKitAspect::deviceTypeId(k);
    for (BuildConfigurationFactory *factory : g_buildConfigurationFactories) {
        if (Utils::mimeTypeForFile(projectPath).matchesName(factory->m_supportedProjectMimeTypeName)
                && factory->supportsTargetDeviceType(deviceType))
            return factory;
    }
    return nullptr;
}

// create
BuildConfigurationFactory * BuildConfigurationFactory::find(Target *parent)
{
    for (BuildConfigurationFactory *factory : g_buildConfigurationFactories) {
        if (factory->canHandle(parent))
            return factory;
    }
    return nullptr;
}

void BuildConfigurationFactory::setSupportedProjectType(Core::Id id)
{
    m_supportedProjectType = id;
}

void BuildConfigurationFactory::setSupportedProjectMimeTypeName(const QString &mimeTypeName)
{
    m_supportedProjectMimeTypeName = mimeTypeName;
}

void BuildConfigurationFactory::addSupportedTargetDeviceType(Core::Id id)
{
    m_supportedTargetDeviceTypes.append(id);
}

bool BuildConfigurationFactory::canHandle(const Target *target) const
{
    if (m_supportedProjectType.isValid() && m_supportedProjectType != target->project()->id())
        return false;

    if (containsType(target->project()->projectIssues(target->kit()), Task::TaskType::Error))
        return false;

    if (!supportsTargetDeviceType(DeviceTypeKitAspect::deviceTypeId(target->kit())))
        return false;

    return true;
}

void BuildConfigurationFactory::setIssueReporter(const IssueReporter &issueReporter)
{
    m_issueReporter = issueReporter;
}

BuildConfiguration *BuildConfigurationFactory::create(Target *parent, const BuildInfo &info) const
{
    if (!canHandle(parent))
        return nullptr;
    QTC_ASSERT(m_creator, return nullptr);
    BuildConfiguration *bc = m_creator(parent);
    if (!bc)
        return nullptr;
    bc->initialize(info);
    return bc;
}

BuildConfiguration *BuildConfigurationFactory::restore(Target *parent, const QVariantMap &map)
{
    const Core::Id id = idFromMap(map);
    for (BuildConfigurationFactory *factory : g_buildConfigurationFactories) {
        QTC_ASSERT(factory->m_creator, return nullptr);
        if (!factory->canHandle(parent))
            continue;
        if (!id.name().startsWith(factory->m_buildConfigId.name()))
            continue;
        BuildConfiguration *bc = factory->m_creator(parent);
        QTC_ASSERT(bc, return nullptr);
        if (!bc->fromMap(map)) {
            delete bc;
            bc = nullptr;
        }
        return bc;
    }
    return nullptr;
}

BuildConfiguration *BuildConfigurationFactory::clone(Target *parent,
                                                      const BuildConfiguration *source)
{
    return restore(parent, source->toMap());
}

} // namespace ProjectExplorer
