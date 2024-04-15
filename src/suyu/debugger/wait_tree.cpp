// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <fmt/format.h>

#include "suyu/debugger/wait_tree.h"
#include "suyu/uisettings.h"

#include "core/arm/debug.h"
#include "core/core.h"
#include "core/hle/kernel/k_class_token.h"
#include "core/hle/kernel/k_handle_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_readable_event.h"
#include "core/hle/kernel/k_scheduler.h"
#include "core/hle/kernel/k_synchronization_object.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/svc_common.h"
#include "core/hle/kernel/svc_types.h"
#include "core/memory.h"

namespace {

constexpr std::array<std::array<Qt::GlobalColor, 2>, 10> WaitTreeColors{{
    {Qt::GlobalColor::darkGreen, Qt::GlobalColor::green},
    {Qt::GlobalColor::darkBlue, Qt::GlobalColor::cyan},
    {Qt::GlobalColor::lightGray, Qt::GlobalColor::lightGray},
    {Qt::GlobalColor::lightGray, Qt::GlobalColor::lightGray},
    {Qt::GlobalColor::darkRed, Qt::GlobalColor::red},
    {Qt::GlobalColor::darkYellow, Qt::GlobalColor::yellow},
    {Qt::GlobalColor::red, Qt::GlobalColor::red},
    {Qt::GlobalColor::darkCyan, Qt::GlobalColor::cyan},
    {Qt::GlobalColor::gray, Qt::GlobalColor::gray},
}};

bool IsDarkTheme() {
    return UISettings::values.theme.contains(QStringLiteral("dark")) ||
           UISettings::values.theme.contains(QStringLiteral("midnight"));
}

} // namespace

WaitTreeItem::WaitTreeItem() = default;
WaitTreeItem::~WaitTreeItem() = default;

QColor WaitTreeItem::GetColor() const {
    if (IsDarkTheme()) {
        return QColor(Qt::GlobalColor::white);
    } else {
        return QColor(Qt::GlobalColor::black);
    }
}

std::vector<std::unique_ptr<WaitTreeItem>> WaitTreeItem::GetChildren() const {
    return {};
}

void WaitTreeItem::Expand() {
    if (IsExpandable() && !expanded) {
        children = GetChildren();
        for (std::size_t i = 0; i < children.size(); ++i) {
            children[i]->parent = this;
            children[i]->row = i;
        }
        expanded = true;
    }
}

WaitTreeItem* WaitTreeItem::Parent() const {
    return parent;
}

const std::vector<std::unique_ptr<WaitTreeItem>>& WaitTreeItem::Children() const {
    return children;
}

bool WaitTreeItem::IsExpandable() const {
    return false;
}

std::size_t WaitTreeItem::Row() const {
    return row;
}

std::vector<std::unique_ptr<WaitTreeThread>> WaitTreeItem::MakeThreadItemList(
    Core::System& system) {
    std::vector<std::unique_ptr<WaitTreeThread>> item_list;
    std::size_t row = 0;
    auto add_threads = [&](const std::vector<Kernel::KThread*>& threads) {
        for (std::size_t i = 0; i < threads.size(); ++i) {
            if (threads[i]->GetThreadType() == Kernel::ThreadType::User) {
                item_list.push_back(std::make_unique<WaitTreeThread>(*threads[i], system));
                item_list.back()->row = row;
            }
            ++row;
        }
    };

    add_threads(system.GlobalSchedulerContext().GetThreadList());

    return item_list;
}

WaitTreeText::WaitTreeText(QString t) : text(std::move(t)) {}
WaitTreeText::~WaitTreeText() = default;

QString WaitTreeText::GetText() const {
    return text;
}

WaitTreeCallstack::WaitTreeCallstack(const Kernel::KThread& thread_, Core::System& system_)
    : thread{thread_}, system{system_} {}
WaitTreeCallstack::~WaitTreeCallstack() = default;

QString WaitTreeCallstack::GetText() const {
    return tr("Call stack");
}

std::vector<std::unique_ptr<WaitTreeItem>> WaitTreeCallstack::GetChildren() const {
    std::vector<std::unique_ptr<WaitTreeItem>> list;

    if (thread.GetThreadType() != Kernel::ThreadType::User) {
        return list;
    }

    if (thread.GetOwnerProcess() == nullptr || !thread.GetOwnerProcess()->Is64Bit()) {
        return list;
    }

    auto backtrace = Core::GetBacktraceFromContext(thread.GetOwnerProcess(), thread.GetContext());

    for (auto& entry : backtrace) {
        std::string s = fmt::format("{:20}{:016X} {:016X} {:016X} {}", entry.module, entry.address,
                                    entry.original_address, entry.offset, entry.name);
        list.push_back(std::make_unique<WaitTreeText>(QString::fromStdString(s)));
    }

    return list;
}

WaitTreeSynchronizationObject::WaitTreeSynchronizationObject(
    const Kernel::KSynchronizationObject& object_, Core::System& system_)
    : object{object_}, system{system_} {}
WaitTreeSynchronizationObject::~WaitTreeSynchronizationObject() = default;

WaitTreeExpandableItem::WaitTreeExpandableItem() = default;
WaitTreeExpandableItem::~WaitTreeExpandableItem() = default;

bool WaitTreeExpandableItem::IsExpandable() const {
    return true;
}

QString WaitTreeSynchronizationObject::GetText() const {
    return tr("[%1] %2")
        .arg(object.GetId())
        .arg(QString::fromStdString(object.GetTypeObj().GetName()));
}

std::unique_ptr<WaitTreeSynchronizationObject> WaitTreeSynchronizationObject::make(
    const Kernel::KSynchronizationObject& object, Core::System& system) {
    const auto type =
        static_cast<Kernel::KClassTokenGenerator::ObjectType>(object.GetTypeObj().GetClassToken());
    switch (type) {
    case Kernel::KClassTokenGenerator::ObjectType::KReadableEvent:
        return std::make_unique<WaitTreeEvent>(static_cast<const Kernel::KReadableEvent&>(object),
                                               system);
    case Kernel::KClassTokenGenerator::ObjectType::KThread:
        return std::make_unique<WaitTreeThread>(static_cast<const Kernel::KThread&>(object),
                                                system);
    default:
        return std::make_unique<WaitTreeSynchronizationObject>(object, system);
    }
}

std::vector<std::unique_ptr<WaitTreeItem>> WaitTreeSynchronizationObject::GetChildren() const {
    std::vector<std::unique_ptr<WaitTreeItem>> list;

    auto threads = object.GetWaitingThreadsForDebugging();
    if (threads.empty()) {
        list.push_back(std::make_unique<WaitTreeText>(tr("waited by no thread")));
    } else {
        list.push_back(std::make_unique<WaitTreeThreadList>(std::move(threads), system));
    }

    return list;
}

WaitTreeThread::WaitTreeThread(const Kernel::KThread& thread, Core::System& system_)
    : WaitTreeSynchronizationObject(thread, system_), system{system_} {}
WaitTreeThread::~WaitTreeThread() = default;

QString WaitTreeThread::GetText() const {
    const auto& thread = static_cast<const Kernel::KThread&>(object);
    QString status;
    switch (thread.GetState()) {
    case Kernel::ThreadState::Runnable:
        if (!thread.IsSuspended()) {
            status = tr("runnable");
        } else {
            status = tr("paused");
        }
        break;
    case Kernel::ThreadState::Waiting:
        switch (thread.GetWaitReasonForDebugging()) {
        case Kernel::ThreadWaitReasonForDebugging::Sleep:
            status = tr("sleeping");
            break;
        case Kernel::ThreadWaitReasonForDebugging::IPC:
            status = tr("waiting for IPC reply");
            break;
        case Kernel::ThreadWaitReasonForDebugging::Synchronization:
            status = tr("waiting for objects");
            break;
        case Kernel::ThreadWaitReasonForDebugging::ConditionVar:
            status = tr("waiting for condition variable");
            break;
        case Kernel::ThreadWaitReasonForDebugging::Arbitration:
            status = tr("waiting for address arbiter");
            break;
        case Kernel::ThreadWaitReasonForDebugging::Suspended:
            status = tr("waiting for suspend resume");
            break;
        default:
            status = tr("waiting");
            break;
        }
        break;
    case Kernel::ThreadState::Initialized:
        status = tr("initialized");
        break;
    case Kernel::ThreadState::Terminated:
        status = tr("terminated");
        break;
    default:
        status = tr("unknown");
        break;
    }

    const auto& context = thread.GetContext();
    const QString pc_info = tr(" PC = 0x%1 LR = 0x%2")
                                .arg(context.pc, 8, 16, QLatin1Char{'0'})
                                .arg(context.lr, 8, 16, QLatin1Char{'0'});
    return QStringLiteral("%1%2 (%3) ")
        .arg(WaitTreeSynchronizationObject::GetText(), pc_info, status);
}

QColor WaitTreeThread::GetColor() const {
    const std::size_t color_index = IsDarkTheme() ? 1 : 0;

    const auto& thread = static_cast<const Kernel::KThread&>(object);
    switch (thread.GetState()) {
    case Kernel::ThreadState::Runnable:
        if (!thread.IsSuspended()) {
            return QColor(WaitTreeColors[0][color_index]);
        } else {
            return QColor(WaitTreeColors[2][color_index]);
        }
    case Kernel::ThreadState::Waiting:
        switch (thread.GetWaitReasonForDebugging()) {
        case Kernel::ThreadWaitReasonForDebugging::IPC:
            return QColor(WaitTreeColors[4][color_index]);
        case Kernel::ThreadWaitReasonForDebugging::Sleep:
            return QColor(WaitTreeColors[5][color_index]);
        case Kernel::ThreadWaitReasonForDebugging::Synchronization:
        case Kernel::ThreadWaitReasonForDebugging::ConditionVar:
        case Kernel::ThreadWaitReasonForDebugging::Arbitration:
        case Kernel::ThreadWaitReasonForDebugging::Suspended:
            return QColor(WaitTreeColors[6][color_index]);
            break;
        default:
            return QColor(WaitTreeColors[3][color_index]);
        }
    case Kernel::ThreadState::Initialized:
        return QColor(WaitTreeColors[7][color_index]);
    case Kernel::ThreadState::Terminated:
        return QColor(WaitTreeColors[8][color_index]);
    default:
        return WaitTreeItem::GetColor();
    }
}

std::vector<std::unique_ptr<WaitTreeItem>> WaitTreeThread::GetChildren() const {
    std::vector<std::unique_ptr<WaitTreeItem>> list(WaitTreeSynchronizationObject::GetChildren());

    const auto& thread = static_cast<const Kernel::KThread&>(object);

    QString processor;
    switch (thread.GetActiveCore()) {
    case Kernel::Svc::IdealCoreUseProcessValue:
        processor = tr("ideal");
        break;
    default:
        processor = tr("core %1").arg(thread.GetActiveCore());
        break;
    }

    list.push_back(std::make_unique<WaitTreeText>(tr("processor = %1").arg(processor)));
    list.push_back(std::make_unique<WaitTreeText>(
        tr("affinity mask = %1").arg(thread.GetAffinityMask().GetAffinityMask())));
    list.push_back(std::make_unique<WaitTreeText>(tr("thread id = %1").arg(thread.GetThreadId())));
    list.push_back(std::make_unique<WaitTreeText>(tr("priority = %1(current) / %2(normal)")
                                                      .arg(thread.GetPriority())
                                                      .arg(thread.GetBasePriority())));
    list.push_back(std::make_unique<WaitTreeText>(
        tr("last running ticks = %1").arg(thread.GetLastScheduledTick())));

    list.push_back(std::make_unique<WaitTreeCallstack>(thread, system));

    return list;
}

WaitTreeEvent::WaitTreeEvent(const Kernel::KReadableEvent& object_, Core::System& system_)
    : WaitTreeSynchronizationObject(object_, system_) {}
WaitTreeEvent::~WaitTreeEvent() = default;

WaitTreeThreadList::WaitTreeThreadList(std::vector<Kernel::KThread*>&& list, Core::System& system_)
    : thread_list(std::move(list)), system{system_} {}
WaitTreeThreadList::~WaitTreeThreadList() = default;

QString WaitTreeThreadList::GetText() const {
    return tr("waited by thread");
}

std::vector<std::unique_ptr<WaitTreeItem>> WaitTreeThreadList::GetChildren() const {
    std::vector<std::unique_ptr<WaitTreeItem>> list(thread_list.size());
    std::transform(thread_list.begin(), thread_list.end(), list.begin(),
                   [this](const auto& t) { return std::make_unique<WaitTreeThread>(*t, system); });
    return list;
}

WaitTreeModel::WaitTreeModel(Core::System& system_, QObject* parent)
    : QAbstractItemModel(parent), system{system_} {}
WaitTreeModel::~WaitTreeModel() = default;

QModelIndex WaitTreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (!hasIndex(row, column, parent))
        return {};

    if (parent.isValid()) {
        WaitTreeItem* parent_item = static_cast<WaitTreeItem*>(parent.internalPointer());
        parent_item->Expand();
        return createIndex(row, column, parent_item->Children()[row].get());
    }

    return createIndex(row, column, thread_items[row].get());
}

QModelIndex WaitTreeModel::parent(const QModelIndex& index) const {
    if (!index.isValid())
        return {};

    WaitTreeItem* parent_item = static_cast<WaitTreeItem*>(index.internalPointer())->Parent();
    if (!parent_item) {
        return QModelIndex();
    }
    return createIndex(static_cast<int>(parent_item->Row()), 0, parent_item);
}

int WaitTreeModel::rowCount(const QModelIndex& parent) const {
    if (!parent.isValid())
        return static_cast<int>(thread_items.size());

    WaitTreeItem* parent_item = static_cast<WaitTreeItem*>(parent.internalPointer());
    parent_item->Expand();
    return static_cast<int>(parent_item->Children().size());
}

int WaitTreeModel::columnCount(const QModelIndex&) const {
    return 1;
}

QVariant WaitTreeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid())
        return {};

    switch (role) {
    case Qt::DisplayRole:
        return static_cast<WaitTreeItem*>(index.internalPointer())->GetText();
    case Qt::ForegroundRole:
        return static_cast<WaitTreeItem*>(index.internalPointer())->GetColor();
    default:
        return {};
    }
}

void WaitTreeModel::ClearItems() {
    thread_items.clear();
}

void WaitTreeModel::InitItems() {
    thread_items = WaitTreeItem::MakeThreadItemList(system);
}

WaitTreeWidget::WaitTreeWidget(Core::System& system_, QWidget* parent)
    : QDockWidget(tr("&Wait Tree"), parent), system{system_} {
    setObjectName(QStringLiteral("WaitTreeWidget"));
    view = new QTreeView(this);
    view->setHeaderHidden(true);
    setWidget(view);
    setEnabled(false);
}

WaitTreeWidget::~WaitTreeWidget() = default;

void WaitTreeWidget::OnDebugModeEntered() {
    if (!system.IsPoweredOn())
        return;
    model->InitItems();
    view->setModel(model);
    setEnabled(true);
}

void WaitTreeWidget::OnDebugModeLeft() {
    setEnabled(false);
    view->setModel(nullptr);
    model->ClearItems();
}

void WaitTreeWidget::OnEmulationStarting(EmuThread* emu_thread) {
    model = new WaitTreeModel(system, this);
    view->setModel(model);
    setEnabled(false);
}

void WaitTreeWidget::OnEmulationStopping() {
    view->setModel(nullptr);
    delete model;
    setEnabled(false);
}
