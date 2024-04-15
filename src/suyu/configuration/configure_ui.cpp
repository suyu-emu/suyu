// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "suyu/configuration/configure_ui.h"

#include <array>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDirIterator>
#include <QFileDialog>
#include <QString>
#include <QToolButton>
#include <QVariant>
#include <QtGlobal>

#include "common/common_types.h"
#include "common/fs/path_util.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "common/settings_enums.h"
#include "core/core.h"
#include "core/frontend/framebuffer_layout.h"
#include "suyu/uisettings.h"
#include "ui_configure_ui.h"

using Settings::DarkModeState;

namespace {
constexpr std::array default_game_icon_sizes{
    std::make_pair(0, QT_TRANSLATE_NOOP("ConfigureUI", "None")),
    std::make_pair(32, QT_TRANSLATE_NOOP("ConfigureUI", "Small (32x32)")),
    std::make_pair(64, QT_TRANSLATE_NOOP("ConfigureUI", "Standard (64x64)")),
    std::make_pair(128, QT_TRANSLATE_NOOP("ConfigureUI", "Large (128x128)")),
    std::make_pair(256, QT_TRANSLATE_NOOP("ConfigureUI", "Full Size (256x256)")),
};

constexpr std::array default_folder_icon_sizes{
    std::make_pair(0, QT_TRANSLATE_NOOP("ConfigureUI", "None")),
    std::make_pair(24, QT_TRANSLATE_NOOP("ConfigureUI", "Small (24x24)")),
    std::make_pair(48, QT_TRANSLATE_NOOP("ConfigureUI", "Standard (48x48)")),
    std::make_pair(72, QT_TRANSLATE_NOOP("ConfigureUI", "Large (72x72)")),
};

// clang-format off
constexpr std::array row_text_names{
    QT_TRANSLATE_NOOP("ConfigureUI", "Filename"),
    QT_TRANSLATE_NOOP("ConfigureUI", "Filetype"),
    QT_TRANSLATE_NOOP("ConfigureUI", "Title ID"),
    QT_TRANSLATE_NOOP("ConfigureUI", "Title Name"),
    QT_TRANSLATE_NOOP("ConfigureUI", "None"),
};
// clang-format on

QString GetTranslatedGameIconSize(size_t index) {
    return QCoreApplication::translate("ConfigureUI", default_game_icon_sizes[index].second);
}

QString GetTranslatedFolderIconSize(size_t index) {
    return QCoreApplication::translate("ConfigureUI", default_folder_icon_sizes[index].second);
}

QString GetTranslatedRowTextName(size_t index) {
    return QCoreApplication::translate("ConfigureUI", row_text_names[index]);
}
} // Anonymous namespace

static float GetUpFactor(Settings::ResolutionSetup res_setup) {
    Settings::ResolutionScalingInfo info{};
    Settings::TranslateResolutionInfo(res_setup, info);
    return info.up_factor;
}

static void PopulateResolutionComboBox(QComboBox* screenshot_height, QWidget* parent) {
    screenshot_height->clear();

    const auto& enumeration =
        Settings::EnumMetadata<Settings::ResolutionSetup>::Canonicalizations();
    std::set<u32> resolutions{};
    for (const auto& [name, value] : enumeration) {
        const float up_factor = GetUpFactor(value);
        u32 height_undocked = Layout::ScreenUndocked::Height * up_factor;
        u32 height_docked = Layout::ScreenDocked::Height * up_factor;
        resolutions.emplace(height_undocked);
        resolutions.emplace(height_docked);
    }

    screenshot_height->addItem(parent->tr("Auto", "Screenshot height option"));
    for (const auto res : resolutions) {
        screenshot_height->addItem(QString::fromStdString(std::to_string(res)));
    }
}

static u32 ScreenshotDimensionToInt(const QString& height) {
    return std::strtoul(height.toUtf8(), nullptr, 0);
}

ConfigureUi::ConfigureUi(Core::System& system_, QWidget* parent)
    : QWidget(parent), ui{std::make_unique<Ui::ConfigureUi>()},
      ratio{Settings::values.aspect_ratio.GetValue()},
      resolution_setting{Settings::values.resolution_setup.GetValue()}, system{system_} {
    ui->setupUi(this);

    InitializeLanguageComboBox();

    for (const auto& theme : UISettings::included_themes) {
        ui->theme_combobox->addItem(QString::fromUtf8(theme.first),
                                    QString::fromUtf8(theme.second));
    }

    // Add custom styles stored in yuzu directory
    const QDir themes_local_dir(
        QString::fromStdString(Common::FS::GetSuyuPathString(Common::FS::SuyuPath::ThemesDir)));
    for (const QString& theme_dir :
         themes_local_dir.entryList(QDir::NoDot | QDir::NoDotDot | QDir::Dirs)) {
        // folders ending with "_dark" are reserved for dark variant icons of other styles
        if (theme_dir.endsWith(QStringLiteral("_dark"))) {
            continue;
        }
        // Split at _ and capitalize words in name
        QStringList cased_name;
        for (QString word : theme_dir.split(QChar::fromLatin1('_'))) {
            cased_name.append(word.at(0).toUpper() + word.mid(1));
        }
        QString theme_name = cased_name.join(QChar::fromLatin1(' '));
        theme_name += QStringLiteral(" (%1)").arg(tr("Custom"));

        ui->theme_combobox->addItem(theme_name, themes_local_dir.filePath(theme_dir));
    }

    InitializeIconSizeComboBox();
    InitializeRowComboBoxes();

    PopulateResolutionComboBox(ui->screenshot_height, this);

    SetConfiguration();

    // Force game list reload if any of the relevant settings are changed.
    connect(ui->show_add_ons, &QCheckBox::stateChanged, this, &ConfigureUi::RequestGameListUpdate);
    connect(ui->show_compat, &QCheckBox::stateChanged, this, &ConfigureUi::RequestGameListUpdate);
    connect(ui->show_size, &QCheckBox::stateChanged, this, &ConfigureUi::RequestGameListUpdate);
    connect(ui->show_types, &QCheckBox::stateChanged, this, &ConfigureUi::RequestGameListUpdate);
    connect(ui->show_play_time, &QCheckBox::stateChanged, this,
            &ConfigureUi::RequestGameListUpdate);
    connect(ui->game_icon_size_combobox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ConfigureUi::RequestGameListUpdate);
    connect(ui->folder_icon_size_combobox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ConfigureUi::RequestGameListUpdate);
    connect(ui->row_1_text_combobox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ConfigureUi::RequestGameListUpdate);
    connect(ui->row_2_text_combobox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ConfigureUi::RequestGameListUpdate);
    // Update available dark mode options depending on selected style
    connect(ui->theme_combobox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ConfigureUi::UpdateDarkModeOptions);

    // Update text ComboBoxes after user interaction.
    connect(ui->row_1_text_combobox, QOverload<int>::of(&QComboBox::activated),
            [this] { ConfigureUi::UpdateSecondRowComboBox(); });
    connect(ui->row_2_text_combobox, QOverload<int>::of(&QComboBox::activated),
            [this] { ConfigureUi::UpdateFirstRowComboBox(); });

    // Set screenshot path to user specification.
    connect(ui->screenshot_path_button, &QToolButton::pressed, this, [this] {
        auto dir =
            QFileDialog::getExistingDirectory(this, tr("Select Screenshots Path..."),
                                              QString::fromStdString(Common::FS::GetSuyuPathString(
                                                  Common::FS::SuyuPath::ScreenshotsDir)));
        if (!dir.isEmpty()) {
            if (dir.back() != QChar::fromLatin1('/')) {
                dir.append(QChar::fromLatin1('/'));
            }

            ui->screenshot_path_edit->setText(dir);
        }
    });

    connect(ui->screenshot_height, &QComboBox::currentTextChanged, [this]() { UpdateWidthText(); });

    UpdateWidthText();
}

ConfigureUi::~ConfigureUi() = default;

void ConfigureUi::ApplyConfiguration() {
    UISettings::values.theme =
        ui->theme_combobox->itemData(ui->theme_combobox->currentIndex()).toString();
    UISettings::values.dark_mode_state =
        static_cast<DarkModeState>(ui->dark_mode_combobox->currentData().toUInt());
    UISettings::values.show_add_ons = ui->show_add_ons->isChecked();
    UISettings::values.show_compat = ui->show_compat->isChecked();
    UISettings::values.show_size = ui->show_size->isChecked();
    UISettings::values.show_types = ui->show_types->isChecked();
    UISettings::values.show_play_time = ui->show_play_time->isChecked();
    UISettings::values.game_icon_size = ui->game_icon_size_combobox->currentData().toUInt();
    UISettings::values.folder_icon_size = ui->folder_icon_size_combobox->currentData().toUInt();
    UISettings::values.row_1_text_id = ui->row_1_text_combobox->currentData().toUInt();
    UISettings::values.row_2_text_id = ui->row_2_text_combobox->currentData().toUInt();

    UISettings::values.enable_screenshot_save_as = ui->enable_screenshot_save_as->isChecked();
    Common::FS::SetSuyuPath(Common::FS::SuyuPath::ScreenshotsDir,
                            ui->screenshot_path_edit->text().toStdString());

    const u32 height = ScreenshotDimensionToInt(ui->screenshot_height->currentText());
    UISettings::values.screenshot_height.SetValue(height);

    RequestGameListUpdate();
    system.ApplySettings();
}

void ConfigureUi::UpdateDarkModeOptions() {
    ui->dark_mode_combobox->clear();

    QString selected_theme = ui->theme_combobox->currentData().toString();

    /* Dark mode option are added according to the modes the current style supports */
    bool has_common_style = QFile::exists(selected_theme + QStringLiteral("/style.qss"));
    bool has_light_style = QFile::exists(selected_theme + QStringLiteral("/light.qss"));
    bool has_dark_style = QFile::exists(selected_theme + QStringLiteral("/dark.qss"));
#ifdef _WIN32
    // Indicate which option needs a restart to be applied, depending on current environment
    // variable
    QByteArray current_qt_qpa = qgetenv("QT_QPA_PLATFORM");
    if (current_qt_qpa.contains("darkmode=2")) {
        if (has_common_style || (has_dark_style && has_light_style)) {
            ui->dark_mode_combobox->addItem(tr("Auto"), QVariant::fromValue(DarkModeState::Auto));
        }
        if (has_common_style || has_dark_style) {
            ui->dark_mode_combobox->addItem(tr("Always On") +
                                                QStringLiteral(" (%1)").arg(tr("Needs restart")),
                                            QVariant::fromValue(DarkModeState::On));
        }
        if (has_common_style || has_light_style) {
            ui->dark_mode_combobox->addItem(tr("Always Off") +
                                                QStringLiteral(" (%1)").arg(tr("Needs restart")),
                                            QVariant::fromValue(DarkModeState::Off));
        }
    } else {
        if (has_common_style || (has_dark_style && has_light_style)) {
            ui->dark_mode_combobox->addItem(tr("Auto") +
                                                QStringLiteral(" (%1)").arg(tr("Needs restart")),
                                            QVariant::fromValue(DarkModeState::Auto));
        }
        if (has_common_style || has_dark_style) {
            ui->dark_mode_combobox->addItem(tr("Always On"),
                                            QVariant::fromValue(DarkModeState::On));
        }
        if (has_common_style || has_light_style) {
            ui->dark_mode_combobox->addItem(tr("Always Off"),
                                            QVariant::fromValue(DarkModeState::Off));
        }
    }
#else
    if (has_common_style || (has_dark_style && has_light_style)) {
        ui->dark_mode_combobox->addItem(tr("Auto"), QVariant::fromValue(DarkModeState::Auto));
    }
    if (has_common_style || has_dark_style) {
        ui->dark_mode_combobox->addItem(tr("Always On"), QVariant::fromValue(DarkModeState::On));
    }
    if (has_common_style || has_light_style) {
        ui->dark_mode_combobox->addItem(tr("Always Off"), QVariant::fromValue(DarkModeState::Off));
    }
#endif
}

void ConfigureUi::RequestGameListUpdate() {
    UISettings::values.is_game_list_reload_pending.exchange(true);
}

void ConfigureUi::SetConfiguration() {
    ui->theme_combobox->setCurrentIndex(ui->theme_combobox->findData(UISettings::values.theme));
    // Dark mode options are populated after the theme is selected, to get the current configuration
    UpdateDarkModeOptions();
    ui->dark_mode_combobox->setCurrentIndex(
        ui->dark_mode_combobox->findData(QVariant::fromValue(UISettings::values.dark_mode_state)));
    ui->language_combobox->setCurrentIndex(ui->language_combobox->findData(
        QString::fromStdString(UISettings::values.language.GetValue())));
    ui->show_add_ons->setChecked(UISettings::values.show_add_ons.GetValue());
    ui->show_compat->setChecked(UISettings::values.show_compat.GetValue());
    ui->show_size->setChecked(UISettings::values.show_size.GetValue());
    ui->show_types->setChecked(UISettings::values.show_types.GetValue());
    ui->show_play_time->setChecked(UISettings::values.show_play_time.GetValue());
    ui->game_icon_size_combobox->setCurrentIndex(
        ui->game_icon_size_combobox->findData(UISettings::values.game_icon_size.GetValue()));
    ui->folder_icon_size_combobox->setCurrentIndex(
        ui->folder_icon_size_combobox->findData(UISettings::values.folder_icon_size.GetValue()));

    ui->enable_screenshot_save_as->setChecked(
        UISettings::values.enable_screenshot_save_as.GetValue());
    ui->screenshot_path_edit->setText(QString::fromStdString(
        Common::FS::GetSuyuPathString(Common::FS::SuyuPath::ScreenshotsDir)));

    const auto height = UISettings::values.screenshot_height.GetValue();
    if (height == 0) {
        ui->screenshot_height->setCurrentIndex(0);
    } else {
        ui->screenshot_height->setCurrentText(QStringLiteral("%1").arg(height));
    }
}

void ConfigureUi::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureUi::RetranslateUI() {
    ui->retranslateUi(this);

    for (int i = 0; i < ui->game_icon_size_combobox->count(); i++) {
        ui->game_icon_size_combobox->setItemText(i,
                                                 GetTranslatedGameIconSize(static_cast<size_t>(i)));
    }

    for (int i = 0; i < ui->folder_icon_size_combobox->count(); i++) {
        ui->folder_icon_size_combobox->setItemText(
            i, GetTranslatedFolderIconSize(static_cast<size_t>(i)));
    }

    for (int i = 0; i < ui->row_1_text_combobox->count(); i++) {
        const QString name = GetTranslatedRowTextName(static_cast<size_t>(i));

        ui->row_1_text_combobox->setItemText(i, name);
        ui->row_2_text_combobox->setItemText(i, name);
    }
}

void ConfigureUi::InitializeLanguageComboBox() {
    ui->language_combobox->addItem(tr("<System>"), QString{});
    ui->language_combobox->addItem(tr("English"), QStringLiteral("en"));
    QDirIterator it(QStringLiteral(":/languages"), QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        QString locale = it.next();
        locale.truncate(locale.lastIndexOf(QLatin1Char{'.'}));
        locale.remove(0, locale.lastIndexOf(QLatin1Char{'/'}) + 1);
        const QString lang = QLocale::languageToString(QLocale(locale).language());
        const QString country = QLocale::countryToString(QLocale(locale).country());
        ui->language_combobox->addItem(QStringLiteral("%1 (%2)").arg(lang, country), locale);
    }

    // Unlike other configuration changes, interface language changes need to be reflected on the
    // interface immediately. This is done by passing a signal to the main window, and then
    // retranslating when passing back.
    connect(ui->language_combobox, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &ConfigureUi::OnLanguageChanged);
}

void ConfigureUi::InitializeIconSizeComboBox() {
    for (size_t i = 0; i < default_game_icon_sizes.size(); i++) {
        const auto size = default_game_icon_sizes[i].first;
        ui->game_icon_size_combobox->addItem(GetTranslatedGameIconSize(i), size);
    }
    for (size_t i = 0; i < default_folder_icon_sizes.size(); i++) {
        const auto size = default_folder_icon_sizes[i].first;
        ui->folder_icon_size_combobox->addItem(GetTranslatedFolderIconSize(i), size);
    }
}

void ConfigureUi::InitializeRowComboBoxes() {
    UpdateFirstRowComboBox(true);
    UpdateSecondRowComboBox(true);
}

void ConfigureUi::UpdateFirstRowComboBox(bool init) {
    const int currentIndex =
        init ? UISettings::values.row_1_text_id.GetValue()
             : ui->row_1_text_combobox->findData(ui->row_1_text_combobox->currentData());

    ui->row_1_text_combobox->clear();

    for (std::size_t i = 0; i < row_text_names.size(); i++) {
        const QString row_text_name = GetTranslatedRowTextName(i);
        ui->row_1_text_combobox->addItem(row_text_name, QVariant::fromValue(i));
    }

    ui->row_1_text_combobox->setCurrentIndex(ui->row_1_text_combobox->findData(currentIndex));

    ui->row_1_text_combobox->removeItem(4); // None
    ui->row_1_text_combobox->removeItem(
        ui->row_1_text_combobox->findData(ui->row_2_text_combobox->currentData()));
}

void ConfigureUi::UpdateSecondRowComboBox(bool init) {
    const int currentIndex =
        init ? UISettings::values.row_2_text_id.GetValue()
             : ui->row_2_text_combobox->findData(ui->row_2_text_combobox->currentData());

    ui->row_2_text_combobox->clear();

    for (std::size_t i = 0; i < row_text_names.size(); ++i) {
        const QString row_text_name = GetTranslatedRowTextName(i);
        ui->row_2_text_combobox->addItem(row_text_name, QVariant::fromValue(i));
    }

    ui->row_2_text_combobox->setCurrentIndex(ui->row_2_text_combobox->findData(currentIndex));

    ui->row_2_text_combobox->removeItem(
        ui->row_2_text_combobox->findData(ui->row_1_text_combobox->currentData()));
}

void ConfigureUi::OnLanguageChanged(int index) {
    if (index == -1)
        return;

    emit LanguageChanged(ui->language_combobox->itemData(index).toString());
}

void ConfigureUi::UpdateWidthText() {
    const u32 height = ScreenshotDimensionToInt(ui->screenshot_height->currentText());
    const u32 width = UISettings::CalculateWidth(height, ratio);
    if (height == 0) {
        const auto up_factor = GetUpFactor(resolution_setting);
        const u32 height_docked = Layout::ScreenDocked::Height * up_factor;
        const u32 width_docked = UISettings::CalculateWidth(height_docked, ratio);
        const u32 height_undocked = Layout::ScreenUndocked::Height * up_factor;
        const u32 width_undocked = UISettings::CalculateWidth(height_undocked, ratio);
        ui->screenshot_width->setText(tr("Auto (%1 x %2, %3 x %4)", "Screenshot width value")
                                          .arg(width_undocked)
                                          .arg(height_undocked)
                                          .arg(width_docked)
                                          .arg(height_docked));
    } else {
        ui->screenshot_width->setText(QStringLiteral("%1 x").arg(width));
    }
}

void ConfigureUi::UpdateScreenshotInfo(Settings::AspectRatio ratio_,
                                       Settings::ResolutionSetup resolution_setting_) {
    ratio = ratio_;
    resolution_setting = resolution_setting_;
    UpdateWidthText();
}
