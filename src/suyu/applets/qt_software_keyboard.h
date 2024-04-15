// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <thread>

#include <QDialog>
#include <QValidator>

#include "core/frontend/applets/software_keyboard.h"

class InputInterpreter;

namespace Core {
class System;
}

namespace Core::HID {
enum class NpadButton : u64;
}

namespace Ui {
class QtSoftwareKeyboardDialog;
}

class GMainWindow;

class QtSoftwareKeyboardDialog final : public QDialog {
    Q_OBJECT

public:
    QtSoftwareKeyboardDialog(QWidget* parent, Core::System& system_, bool is_inline_,
                             Core::Frontend::KeyboardInitializeParameters initialize_parameters_);
    ~QtSoftwareKeyboardDialog() override;

    void ShowNormalKeyboard(QPoint pos, QSize size);

    void ShowTextCheckDialog(Service::AM::Frontend::SwkbdTextCheckResult text_check_result,
                             std::u16string text_check_message);

    void ShowInlineKeyboard(Core::Frontend::InlineAppearParameters appear_parameters, QPoint pos,
                            QSize size);

    void HideInlineKeyboard();

    void InlineTextChanged(Core::Frontend::InlineTextParameters text_parameters);

    void ExitKeyboard();

signals:
    void SubmitNormalText(Service::AM::Frontend::SwkbdResult result, std::u16string submitted_text,
                          bool confirmed = false) const;

    void SubmitInlineText(Service::AM::Frontend::SwkbdReplyType reply_type,
                          std::u16string submitted_text, s32 cursor_position) const;

public slots:
    void open() override;
    void reject() override;

protected:
    /// We override the keyPressEvent for inputting text into the inline software keyboard.
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum class Direction {
        Left,
        Up,
        Right,
        Down,
    };

    enum class BottomOSKIndex {
        LowerCase,
        UpperCase,
        NumberPad,
    };

    /**
     * Get the current theme suffix
     * @return an empty string for light theme, "_dark" for dark theme
     */
    QString theme();

    /**
     * Moves and resizes the window to a specified position and size.
     *
     * @param pos Top-left window position
     * @param size Window size
     */
    void MoveAndResizeWindow(QPoint pos, QSize size);

    /**
     * Rescales all keyboard elements to account for High DPI displays.
     *
     * @param width Window width
     * @param height Window height
     * @param dpi_scale Display scaling factor
     */
    void RescaleKeyboardElements(float width, float height, float dpi_scale);

    /// Sets the keyboard type based on initialize_parameters.
    void SetKeyboardType();

    /// Sets the password mode based on initialize_parameters.
    void SetPasswordMode();

    /// Sets the text draw type based on initialize_parameters.
    void SetTextDrawType();

    /// Sets the controller image at the bottom left of the software keyboard.
    void SetControllerImage();

    /// Sets the controller image at the bottom left of the software keyboard.
    void SetButtonImages();

    /// Disables buttons based on initialize_parameters.
    void DisableKeyboardButtons();

    /// Changes whether the backspace or/and ok buttons should be enabled or disabled.
    void SetBackspaceOkEnabled();

    /**
     * Validates the input text sent in based on the parameters in initialize_parameters.
     *
     * @param input_text Input text
     *
     * @returns True if the input text is valid, false otherwise.
     */
    bool ValidateInputText(const QString& input_text);

    /// Switches between LowerCase and UpperCase (Shift and Caps Lock)
    void ChangeBottomOSKIndex();

    /// Processes a keyboard button click from the UI as normal keyboard input.
    void NormalKeyboardButtonClicked(QPushButton* button);

    /// Processes a keyboard button click from the UI as inline keyboard input.
    void InlineKeyboardButtonClicked(QPushButton* button);

    /**
     * Inserts a string of arbitrary length into the current_text at the current cursor position.
     * This is only used for the inline software keyboard.
     */
    void InlineTextInsertString(std::u16string_view string);

    /// Setup the mouse hover workaround for "focusing" buttons. This should only be called once.
    void SetupMouseHover();

    /**
     * Handles button presses and converts them into keyboard input.
     *
     * @tparam HIDButton The list of buttons that can be converted into keyboard input.
     */
    template <Core::HID::NpadButton... T>
    void HandleButtonPressedOnce();

    /**
     * Handles button holds and converts them into keyboard input.
     *
     * @tparam HIDButton The list of buttons that can be converted into keyboard input.
     */
    template <Core::HID::NpadButton... T>
    void HandleButtonHold();

    /**
     * Translates a button press to focus or click a keyboard button.
     *
     * @param button The button press to process.
     */
    void TranslateButtonPress(Core::HID::NpadButton button);

    /**
     * Moves the focus of a button in a certain direction.
     *
     * @param direction The direction to move.
     */
    void MoveButtonDirection(Direction direction);

    /**
     * Moves the text cursor in a certain direction.
     *
     * @param direction The direction to move.
     */
    void MoveTextCursorDirection(Direction direction);

    void StartInputThread();
    void StopInputThread();

    /// The thread where input is being polled and processed.
    void InputThread();

    std::unique_ptr<Ui::QtSoftwareKeyboardDialog> ui;

    Core::System& system;

    // True if it is the inline software keyboard.
    bool is_inline;

    // Common software keyboard initialize parameters.
    Core::Frontend::KeyboardInitializeParameters initialize_parameters;

    // Used only by the inline software keyboard since the QLineEdit or QTextEdit is hidden.
    std::u16string current_text;
    s32 cursor_position{0};

    static constexpr std::size_t NUM_ROWS_NORMAL = 5;
    static constexpr std::size_t NUM_COLUMNS_NORMAL = 12;
    static constexpr std::size_t NUM_ROWS_NUMPAD = 4;
    static constexpr std::size_t NUM_COLUMNS_NUMPAD = 4;

    // Stores the normal keyboard layout.
    std::array<std::array<std::array<QPushButton*, NUM_COLUMNS_NORMAL>, NUM_ROWS_NORMAL>, 2>
        keyboard_buttons;
    // Stores the numberpad keyboard layout.
    std::array<std::array<QPushButton*, NUM_COLUMNS_NUMPAD>, NUM_ROWS_NUMPAD> numberpad_buttons;

    // Contains a set of all buttons used in keyboard_buttons and numberpad_buttons.
    std::array<QPushButton*, 112> all_buttons;

    std::size_t row{0};
    std::size_t column{0};

    BottomOSKIndex bottom_osk_index{BottomOSKIndex::LowerCase};
    std::atomic<bool> caps_lock_enabled{false};

    std::unique_ptr<InputInterpreter> input_interpreter;

    std::thread input_thread;

    std::atomic<bool> input_thread_running{};
};

class QtSoftwareKeyboard final : public QObject, public Core::Frontend::SoftwareKeyboardApplet {
    Q_OBJECT

public:
    explicit QtSoftwareKeyboard(GMainWindow& parent);
    ~QtSoftwareKeyboard() override;

    void Close() const override {
        ExitKeyboard();
    }

    void InitializeKeyboard(bool is_inline,
                            Core::Frontend::KeyboardInitializeParameters initialize_parameters,
                            SubmitNormalCallback submit_normal_callback_,
                            SubmitInlineCallback submit_inline_callback_) override;

    void ShowNormalKeyboard() const override;

    void ShowTextCheckDialog(Service::AM::Frontend::SwkbdTextCheckResult text_check_result,
                             std::u16string text_check_message) const override;

    void ShowInlineKeyboard(
        Core::Frontend::InlineAppearParameters appear_parameters) const override;

    void HideInlineKeyboard() const override;

    void InlineTextChanged(Core::Frontend::InlineTextParameters text_parameters) const override;

    void ExitKeyboard() const override;

signals:
    void MainWindowInitializeKeyboard(
        bool is_inline, Core::Frontend::KeyboardInitializeParameters initialize_parameters) const;

    void MainWindowShowNormalKeyboard() const;

    void MainWindowShowTextCheckDialog(
        Service::AM::Frontend::SwkbdTextCheckResult text_check_result,
        std::u16string text_check_message) const;

    void MainWindowShowInlineKeyboard(
        Core::Frontend::InlineAppearParameters appear_parameters) const;

    void MainWindowHideInlineKeyboard() const;

    void MainWindowInlineTextChanged(Core::Frontend::InlineTextParameters text_parameters) const;

    void MainWindowExitKeyboard() const;

private:
    void SubmitNormalText(Service::AM::Frontend::SwkbdResult result, std::u16string submitted_text,
                          bool confirmed) const;

    void SubmitInlineText(Service::AM::Frontend::SwkbdReplyType reply_type,
                          std::u16string submitted_text, s32 cursor_position) const;

    mutable SubmitNormalCallback submit_normal_callback;
    mutable SubmitInlineCallback submit_inline_callback;
};
