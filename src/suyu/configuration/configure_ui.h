// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QWidget>
#include "common/settings_enums.h"

namespace Core {
class System;
}

namespace Ui {
class ConfigureUi;
}

class ConfigureUi : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureUi(Core::System& system_, QWidget* parent = nullptr);
    ~ConfigureUi() override;

    void ApplyConfiguration();

    void UpdateScreenshotInfo(Settings::AspectRatio ratio,
                              Settings::ResolutionSetup resolution_info);

private slots:
    void OnLanguageChanged(int index);

signals:
    void LanguageChanged(const QString& locale);

private:
    void UpdateDarkModeOptions();
    void RequestGameListUpdate();

    void SetConfiguration();

    void changeEvent(QEvent*) override;
    void RetranslateUI();

    void InitializeLanguageComboBox();
    void InitializeIconSizeComboBox();
    void InitializeRowComboBoxes();

    void UpdateFirstRowComboBox(bool init = false);
    void UpdateSecondRowComboBox(bool init = false);

    void UpdateWidthText();

    std::unique_ptr<Ui::ConfigureUi> ui;

    Settings::AspectRatio ratio;
    Settings::ResolutionSetup resolution_setting;
    Core::System& system;
};
