// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QCursor>
#include <QKeyEvent>
#include <QScreen>

#include "common/logging/log.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/frontend/input_interpreter.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_types.h"
#include "suyu/applets/qt_software_keyboard.h"
#include "suyu/main.h"
#include "suyu/util/overlay_dialog.h"
#include "ui_qt_software_keyboard.h"

namespace {

using namespace Service::AM::Frontend;

constexpr float BASE_HEADER_FONT_SIZE = 23.0f;
constexpr float BASE_SUB_FONT_SIZE = 17.0f;
constexpr float BASE_EDITOR_FONT_SIZE = 26.0f;
constexpr float BASE_CHAR_BUTTON_FONT_SIZE = 28.0f;
constexpr float BASE_LABEL_BUTTON_FONT_SIZE = 18.0f;
constexpr float BASE_ICON_BUTTON_SIZE = 36.0f;
[[maybe_unused]] constexpr float BASE_WIDTH = 1280.0f;
constexpr float BASE_HEIGHT = 720.0f;

} // Anonymous namespace

QtSoftwareKeyboardDialog::QtSoftwareKeyboardDialog(
    QWidget* parent, Core::System& system_, bool is_inline_,
    Core::Frontend::KeyboardInitializeParameters initialize_parameters_)
    : QDialog(parent), ui{std::make_unique<Ui::QtSoftwareKeyboardDialog>()}, system{system_},
      is_inline{is_inline_}, initialize_parameters{std::move(initialize_parameters_)} {
    ui->setupUi(this);

    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowTitleHint |
                   Qt::WindowSystemMenuHint | Qt::CustomizeWindowHint);
    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground);

    keyboard_buttons = {{
        {{
            {
                ui->button_1,
                ui->button_2,
                ui->button_3,
                ui->button_4,
                ui->button_5,
                ui->button_6,
                ui->button_7,
                ui->button_8,
                ui->button_9,
                ui->button_0,
                ui->button_minus,
                ui->button_backspace,
            },
            {
                ui->button_q,
                ui->button_w,
                ui->button_e,
                ui->button_r,
                ui->button_t,
                ui->button_y,
                ui->button_u,
                ui->button_i,
                ui->button_o,
                ui->button_p,
                ui->button_slash,
                ui->button_return,
            },
            {
                ui->button_a,
                ui->button_s,
                ui->button_d,
                ui->button_f,
                ui->button_g,
                ui->button_h,
                ui->button_j,
                ui->button_k,
                ui->button_l,
                ui->button_colon,
                ui->button_apostrophe,
                ui->button_return,
            },
            {
                ui->button_z,
                ui->button_x,
                ui->button_c,
                ui->button_v,
                ui->button_b,
                ui->button_n,
                ui->button_m,
                ui->button_comma,
                ui->button_dot,
                ui->button_question,
                ui->button_exclamation,
                ui->button_ok,
            },
            {
                ui->button_shift,
                ui->button_shift,
                ui->button_space,
                ui->button_space,
                ui->button_space,
                ui->button_space,
                ui->button_space,
                ui->button_space,
                ui->button_space,
                ui->button_space,
                ui->button_space,
                ui->button_ok,
            },
        }},
        {{
            {
                ui->button_hash,
                ui->button_left_bracket,
                ui->button_right_bracket,
                ui->button_dollar,
                ui->button_percent,
                ui->button_circumflex,
                ui->button_ampersand,
                ui->button_asterisk,
                ui->button_left_parenthesis,
                ui->button_right_parenthesis,
                ui->button_underscore,
                ui->button_backspace_shift,
            },
            {
                ui->button_q_shift,
                ui->button_w_shift,
                ui->button_e_shift,
                ui->button_r_shift,
                ui->button_t_shift,
                ui->button_y_shift,
                ui->button_u_shift,
                ui->button_i_shift,
                ui->button_o_shift,
                ui->button_p_shift,
                ui->button_at,
                ui->button_return_shift,
            },
            {
                ui->button_a_shift,
                ui->button_s_shift,
                ui->button_d_shift,
                ui->button_f_shift,
                ui->button_g_shift,
                ui->button_h_shift,
                ui->button_j_shift,
                ui->button_k_shift,
                ui->button_l_shift,
                ui->button_semicolon,
                ui->button_quotation,
                ui->button_return_shift,
            },
            {
                ui->button_z_shift,
                ui->button_x_shift,
                ui->button_c_shift,
                ui->button_v_shift,
                ui->button_b_shift,
                ui->button_n_shift,
                ui->button_m_shift,
                ui->button_less_than,
                ui->button_greater_than,
                ui->button_plus,
                ui->button_equal,
                ui->button_ok_shift,
            },
            {
                ui->button_shift_shift,
                ui->button_shift_shift,
                ui->button_space_shift,
                ui->button_space_shift,
                ui->button_space_shift,
                ui->button_space_shift,
                ui->button_space_shift,
                ui->button_space_shift,
                ui->button_space_shift,
                ui->button_space_shift,
                ui->button_space_shift,
                ui->button_ok_shift,
            },
        }},
    }};

    numberpad_buttons = {{
        {
            ui->button_1_num,
            ui->button_2_num,
            ui->button_3_num,
            ui->button_backspace_num,
        },
        {
            ui->button_4_num,
            ui->button_5_num,
            ui->button_6_num,
            ui->button_ok_num,
        },
        {
            ui->button_7_num,
            ui->button_8_num,
            ui->button_9_num,
            ui->button_ok_num,
        },
        {
            ui->button_left_optional_num,
            ui->button_0_num,
            ui->button_right_optional_num,
            ui->button_ok_num,
        },
    }};

    all_buttons = {
        ui->button_1,
        ui->button_2,
        ui->button_3,
        ui->button_4,
        ui->button_5,
        ui->button_6,
        ui->button_7,
        ui->button_8,
        ui->button_9,
        ui->button_0,
        ui->button_minus,
        ui->button_backspace,
        ui->button_q,
        ui->button_w,
        ui->button_e,
        ui->button_r,
        ui->button_t,
        ui->button_y,
        ui->button_u,
        ui->button_i,
        ui->button_o,
        ui->button_p,
        ui->button_slash,
        ui->button_return,
        ui->button_a,
        ui->button_s,
        ui->button_d,
        ui->button_f,
        ui->button_g,
        ui->button_h,
        ui->button_j,
        ui->button_k,
        ui->button_l,
        ui->button_colon,
        ui->button_apostrophe,
        ui->button_z,
        ui->button_x,
        ui->button_c,
        ui->button_v,
        ui->button_b,
        ui->button_n,
        ui->button_m,
        ui->button_comma,
        ui->button_dot,
        ui->button_question,
        ui->button_exclamation,
        ui->button_ok,
        ui->button_shift,
        ui->button_space,
        ui->button_hash,
        ui->button_left_bracket,
        ui->button_right_bracket,
        ui->button_dollar,
        ui->button_percent,
        ui->button_circumflex,
        ui->button_ampersand,
        ui->button_asterisk,
        ui->button_left_parenthesis,
        ui->button_right_parenthesis,
        ui->button_underscore,
        ui->button_backspace_shift,
        ui->button_q_shift,
        ui->button_w_shift,
        ui->button_e_shift,
        ui->button_r_shift,
        ui->button_t_shift,
        ui->button_y_shift,
        ui->button_u_shift,
        ui->button_i_shift,
        ui->button_o_shift,
        ui->button_p_shift,
        ui->button_at,
        ui->button_return_shift,
        ui->button_a_shift,
        ui->button_s_shift,
        ui->button_d_shift,
        ui->button_f_shift,
        ui->button_g_shift,
        ui->button_h_shift,
        ui->button_j_shift,
        ui->button_k_shift,
        ui->button_l_shift,
        ui->button_semicolon,
        ui->button_quotation,
        ui->button_z_shift,
        ui->button_x_shift,
        ui->button_c_shift,
        ui->button_v_shift,
        ui->button_b_shift,
        ui->button_n_shift,
        ui->button_m_shift,
        ui->button_less_than,
        ui->button_greater_than,
        ui->button_plus,
        ui->button_equal,
        ui->button_ok_shift,
        ui->button_shift_shift,
        ui->button_space_shift,
        ui->button_1_num,
        ui->button_2_num,
        ui->button_3_num,
        ui->button_backspace_num,
        ui->button_4_num,
        ui->button_5_num,
        ui->button_6_num,
        ui->button_ok_num,
        ui->button_7_num,
        ui->button_8_num,
        ui->button_9_num,
        ui->button_left_optional_num,
        ui->button_0_num,
        ui->button_right_optional_num,
    };

    SetupMouseHover();

    if (!initialize_parameters.ok_text.empty()) {
        ui->button_ok->setText(QString::fromStdU16String(initialize_parameters.ok_text));
    }

    ui->label_header->setText(QString::fromStdU16String(initialize_parameters.header_text));
    ui->label_sub->setText(QString::fromStdU16String(initialize_parameters.sub_text));

    ui->button_left_optional_num->setText(QChar{initialize_parameters.left_optional_symbol_key});
    ui->button_right_optional_num->setText(QChar{initialize_parameters.right_optional_symbol_key});

    current_text = initialize_parameters.initial_text;
    cursor_position = initialize_parameters.initial_cursor_position;

    SetTextDrawType();

    for (auto* button : all_buttons) {
        connect(button, &QPushButton::clicked, this, [this, button](bool) {
            if (is_inline) {
                InlineKeyboardButtonClicked(button);
            } else {
                NormalKeyboardButtonClicked(button);
            }
        });
    }

    // TODO (Morph): Remove this when InputInterpreter no longer relies on the HID backend
    if (system.IsPoweredOn()) {
        input_interpreter = std::make_unique<InputInterpreter>(system);
    }
}

QtSoftwareKeyboardDialog::~QtSoftwareKeyboardDialog() {
    StopInputThread();
}

QString QtSoftwareKeyboardDialog::theme() {
    if (GMainWindow::CheckDarkMode()) {
        return QStringLiteral("_dark");
    } else {
        return QString{};
    }
}

void QtSoftwareKeyboardDialog::ShowNormalKeyboard(QPoint pos, QSize size) {
    if (isVisible()) {
        return;
    }

    MoveAndResizeWindow(pos, size);

    SetKeyboardType();
    SetPasswordMode();
    SetControllerImage();
    SetButtonImages();
    DisableKeyboardButtons();
    SetBackspaceOkEnabled();

    open();
}

void QtSoftwareKeyboardDialog::ShowTextCheckDialog(
    Service::AM::Frontend::SwkbdTextCheckResult text_check_result,
    std::u16string text_check_message) {
    switch (text_check_result) {
    case SwkbdTextCheckResult::Success:
    case SwkbdTextCheckResult::Silent:
    default:
        break;
    case SwkbdTextCheckResult::Failure: {
        StopInputThread();

        OverlayDialog dialog(this, system, QString{}, QString::fromStdU16String(text_check_message),
                             QString{}, tr("OK"), Qt::AlignCenter);
        dialog.exec();

        StartInputThread();
        break;
    }
    case SwkbdTextCheckResult::Confirm: {
        StopInputThread();

        OverlayDialog dialog(this, system, QString{}, QString::fromStdU16String(text_check_message),
                             tr("Cancel"), tr("OK"), Qt::AlignCenter);
        if (dialog.exec() != QDialog::Accepted) {
            StartInputThread();
            break;
        }

        const auto text = ui->topOSK->currentIndex() == 1 ? ui->text_edit_osk->toPlainText()
                                                          : ui->line_edit_osk->text();
        auto text_str = Common::U16StringFromBuffer(text.utf16(), text.size());

        emit SubmitNormalText(SwkbdResult::Ok, std::move(text_str), true);
        break;
    }
    }
}

void QtSoftwareKeyboardDialog::ShowInlineKeyboard(
    Core::Frontend::InlineAppearParameters appear_parameters, QPoint pos, QSize size) {
    MoveAndResizeWindow(pos, size);

    ui->topOSK->setStyleSheet(QStringLiteral("background: rgba(0, 0, 0, 0);"));

    ui->headerOSK->hide();
    ui->subOSK->hide();
    ui->inputOSK->hide();
    ui->charactersOSK->hide();
    ui->inputBoxOSK->hide();
    ui->charactersBoxOSK->hide();

    initialize_parameters.max_text_length = appear_parameters.max_text_length;
    initialize_parameters.min_text_length = appear_parameters.min_text_length;
    initialize_parameters.type = appear_parameters.type;
    initialize_parameters.key_disable_flags = appear_parameters.key_disable_flags;
    initialize_parameters.enable_backspace_button = appear_parameters.enable_backspace_button;
    initialize_parameters.enable_return_button = appear_parameters.enable_return_button;
    initialize_parameters.disable_cancel_button = appear_parameters.disable_cancel_button;

    SetKeyboardType();
    SetControllerImage();
    SetButtonImages();
    DisableKeyboardButtons();
    SetBackspaceOkEnabled();

    open();
}

void QtSoftwareKeyboardDialog::HideInlineKeyboard() {
    StopInputThread();
    QDialog::hide();
}

void QtSoftwareKeyboardDialog::InlineTextChanged(
    Core::Frontend::InlineTextParameters text_parameters) {
    current_text = text_parameters.input_text;
    cursor_position = text_parameters.cursor_position;

    SetBackspaceOkEnabled();
}

void QtSoftwareKeyboardDialog::ExitKeyboard() {
    StopInputThread();
    QDialog::done(QDialog::Accepted);
}

void QtSoftwareKeyboardDialog::open() {
    QDialog::open();

    row = 0;
    column = 0;

    switch (bottom_osk_index) {
    case BottomOSKIndex::LowerCase:
    case BottomOSKIndex::UpperCase: {
        const auto* const curr_button =
            keyboard_buttons[static_cast<std::size_t>(bottom_osk_index)][row][column];

        // This is a workaround for setFocus() randomly not showing focus in the UI
        QCursor::setPos(curr_button->mapToGlobal(curr_button->rect().center()));
        break;
    }
    case BottomOSKIndex::NumberPad: {
        const auto* const curr_button = numberpad_buttons[row][column];

        // This is a workaround for setFocus() randomly not showing focus in the UI
        QCursor::setPos(curr_button->mapToGlobal(curr_button->rect().center()));
        break;
    }
    default:
        break;
    }

    StartInputThread();
}

void QtSoftwareKeyboardDialog::reject() {
    // Pressing the ESC key in a dialog calls QDialog::reject().
    // We will override this behavior to the "Cancel" action on the software keyboard.
    TranslateButtonPress(Core::HID::NpadButton::X);
}

void QtSoftwareKeyboardDialog::keyPressEvent(QKeyEvent* event) {
    if (!is_inline) {
        QDialog::keyPressEvent(event);
        return;
    }

    const auto entered_key = event->key();

    switch (entered_key) {
    case Qt::Key_Escape:
        QDialog::keyPressEvent(event);
        return;
    case Qt::Key_Backspace:
        switch (bottom_osk_index) {
        case BottomOSKIndex::LowerCase:
            ui->button_backspace->click();
            break;
        case BottomOSKIndex::UpperCase:
            ui->button_backspace_shift->click();
            break;
        case BottomOSKIndex::NumberPad:
            ui->button_backspace_num->click();
            break;
        default:
            break;
        }
        return;
    case Qt::Key_Return:
        switch (bottom_osk_index) {
        case BottomOSKIndex::LowerCase:
            ui->button_ok->click();
            break;
        case BottomOSKIndex::UpperCase:
            ui->button_ok_shift->click();
            break;
        case BottomOSKIndex::NumberPad:
            ui->button_ok_num->click();
            break;
        default:
            break;
        }
        return;
    case Qt::Key_Left:
        MoveTextCursorDirection(Direction::Left);
        return;
    case Qt::Key_Right:
        MoveTextCursorDirection(Direction::Right);
        return;
    default:
        break;
    }

    const auto entered_text = event->text();

    if (entered_text.isEmpty()) {
        return;
    }

    InlineTextInsertString(Common::U16StringFromBuffer(entered_text.utf16(), entered_text.size()));
}

void QtSoftwareKeyboardDialog::MoveAndResizeWindow(QPoint pos, QSize size) {
    QDialog::move(pos);
    QDialog::resize(size);

    // High DPI
    const float dpi_scale = screen()->logicalDotsPerInch() / 96.0f;

    RescaleKeyboardElements(size.width(), size.height(), dpi_scale);
}

void QtSoftwareKeyboardDialog::RescaleKeyboardElements(float width, float height, float dpi_scale) {
    const auto header_font_size = BASE_HEADER_FONT_SIZE * (height / BASE_HEIGHT) / dpi_scale;
    const auto sub_font_size = BASE_SUB_FONT_SIZE * (height / BASE_HEIGHT) / dpi_scale;
    const auto editor_font_size = BASE_EDITOR_FONT_SIZE * (height / BASE_HEIGHT) / dpi_scale;
    const auto char_button_font_size =
        BASE_CHAR_BUTTON_FONT_SIZE * (height / BASE_HEIGHT) / dpi_scale;
    const auto label_button_font_size =
        BASE_LABEL_BUTTON_FONT_SIZE * (height / BASE_HEIGHT) / dpi_scale;

    QFont header_font(QStringLiteral("MS Shell Dlg 2"), header_font_size, QFont::Normal);
    QFont sub_font(QStringLiteral("MS Shell Dlg 2"), sub_font_size, QFont::Normal);
    QFont editor_font(QStringLiteral("MS Shell Dlg 2"), editor_font_size, QFont::Normal);
    QFont char_button_font(QStringLiteral("MS Shell Dlg 2"), char_button_font_size, QFont::Normal);
    QFont label_button_font(QStringLiteral("MS Shell Dlg 2"), label_button_font_size,
                            QFont::Normal);

    ui->label_header->setFont(header_font);
    ui->label_sub->setFont(sub_font);
    ui->line_edit_osk->setFont(editor_font);
    ui->text_edit_osk->setFont(editor_font);
    ui->label_characters->setFont(sub_font);
    ui->label_characters_box->setFont(sub_font);

    ui->label_shift->setFont(label_button_font);
    ui->label_shift_shift->setFont(label_button_font);
    ui->label_cancel->setFont(label_button_font);
    ui->label_cancel_shift->setFont(label_button_font);
    ui->label_cancel_num->setFont(label_button_font);
    ui->label_enter->setFont(label_button_font);
    ui->label_enter_shift->setFont(label_button_font);
    ui->label_enter_num->setFont(label_button_font);

    for (auto* button : all_buttons) {
        if (button == ui->button_return || button == ui->button_return_shift) {
            button->setFont(label_button_font);
            continue;
        }

        if (button == ui->button_space || button == ui->button_space_shift) {
            button->setFont(label_button_font);
            continue;
        }

        if (button == ui->button_shift || button == ui->button_shift_shift) {
            button->setFont(label_button_font);
            button->setIconSize(QSize(BASE_ICON_BUTTON_SIZE, BASE_ICON_BUTTON_SIZE) *
                                (height / BASE_HEIGHT));
            continue;
        }

        if (button == ui->button_backspace || button == ui->button_backspace_shift ||
            button == ui->button_backspace_num) {
            button->setFont(label_button_font);
            button->setIconSize(QSize(BASE_ICON_BUTTON_SIZE, BASE_ICON_BUTTON_SIZE) *
                                (height / BASE_HEIGHT));
            continue;
        }

        if (button == ui->button_ok || button == ui->button_ok_shift ||
            button == ui->button_ok_num) {
            button->setFont(label_button_font);
            continue;
        }

        button->setFont(char_button_font);
    }
}

void QtSoftwareKeyboardDialog::SetKeyboardType() {
    switch (initialize_parameters.type) {
    case SwkbdType::Normal:
    case SwkbdType::Qwerty:
    case SwkbdType::Unknown3:
    case SwkbdType::Latin:
    case SwkbdType::SimplifiedChinese:
    case SwkbdType::TraditionalChinese:
    case SwkbdType::Korean:
    default: {
        bottom_osk_index = BottomOSKIndex::LowerCase;
        ui->bottomOSK->setCurrentIndex(static_cast<int>(bottom_osk_index));

        ui->verticalLayout_2->setStretch(0, 320);
        ui->verticalLayout_2->setStretch(1, 400);

        ui->gridLineOSK->setRowStretch(5, 94);
        ui->gridBoxOSK->setRowStretch(2, 81);
        break;
    }
    case SwkbdType::NumberPad: {
        bottom_osk_index = BottomOSKIndex::NumberPad;
        ui->bottomOSK->setCurrentIndex(static_cast<int>(bottom_osk_index));

        ui->verticalLayout_2->setStretch(0, 370);
        ui->verticalLayout_2->setStretch(1, 350);

        ui->gridLineOSK->setRowStretch(5, 144);
        ui->gridBoxOSK->setRowStretch(2, 131);
        break;
    }
    }
}

void QtSoftwareKeyboardDialog::SetPasswordMode() {
    switch (initialize_parameters.password_mode) {
    case SwkbdPasswordMode::Disabled:
    default:
        ui->line_edit_osk->setEchoMode(QLineEdit::Normal);
        break;
    case SwkbdPasswordMode::Enabled:
        ui->line_edit_osk->setEchoMode(QLineEdit::Password);
        break;
    }
}

void QtSoftwareKeyboardDialog::SetTextDrawType() {
    switch (initialize_parameters.text_draw_type) {
    case SwkbdTextDrawType::Line:
    case SwkbdTextDrawType::DownloadCode: {
        ui->topOSK->setCurrentIndex(0);

        if (initialize_parameters.max_text_length <= 10) {
            ui->gridLineOSK->setColumnStretch(0, 390);
            ui->gridLineOSK->setColumnStretch(1, 500);
            ui->gridLineOSK->setColumnStretch(2, 390);
        } else {
            ui->gridLineOSK->setColumnStretch(0, 130);
            ui->gridLineOSK->setColumnStretch(1, 1020);
            ui->gridLineOSK->setColumnStretch(2, 130);
        }

        if (is_inline) {
            return;
        }

        connect(ui->line_edit_osk, &QLineEdit::textChanged, [this](const QString& changed_string) {
            const auto is_valid = ValidateInputText(changed_string);

            const auto text_length = static_cast<u32>(changed_string.length());

            ui->label_characters->setText(QStringLiteral("%1/%2")
                                              .arg(text_length)
                                              .arg(initialize_parameters.max_text_length));

            ui->button_ok->setEnabled(is_valid);
            ui->button_ok_shift->setEnabled(is_valid);
            ui->button_ok_num->setEnabled(is_valid);

            ui->line_edit_osk->setFocus();
        });

        connect(ui->line_edit_osk, &QLineEdit::cursorPositionChanged,
                [this](int old_cursor_position, int new_cursor_position) {
                    ui->button_backspace->setEnabled(
                        initialize_parameters.enable_backspace_button && new_cursor_position > 0);
                    ui->button_backspace_shift->setEnabled(
                        initialize_parameters.enable_backspace_button && new_cursor_position > 0);
                    ui->button_backspace_num->setEnabled(
                        initialize_parameters.enable_backspace_button && new_cursor_position > 0);

                    ui->line_edit_osk->setFocus();
                });

        connect(
            ui->line_edit_osk, &QLineEdit::returnPressed, this,
            [this] { TranslateButtonPress(Core::HID::NpadButton::Plus); }, Qt::QueuedConnection);

        ui->line_edit_osk->setPlaceholderText(
            QString::fromStdU16String(initialize_parameters.guide_text));
        ui->line_edit_osk->setText(QString::fromStdU16String(initialize_parameters.initial_text));
        ui->line_edit_osk->setMaxLength(initialize_parameters.max_text_length);
        ui->line_edit_osk->setCursorPosition(initialize_parameters.initial_cursor_position);

        ui->label_characters->setText(QStringLiteral("%1/%2")
                                          .arg(initialize_parameters.initial_text.size())
                                          .arg(initialize_parameters.max_text_length));
        break;
    }
    case SwkbdTextDrawType::Box:
    default: {
        ui->topOSK->setCurrentIndex(1);

        if (is_inline) {
            return;
        }

        connect(ui->text_edit_osk, &QTextEdit::textChanged, [this] {
            if (static_cast<u32>(ui->text_edit_osk->toPlainText().length()) >
                initialize_parameters.max_text_length) {
                auto text_cursor = ui->text_edit_osk->textCursor();
                ui->text_edit_osk->setTextCursor(text_cursor);
                text_cursor.deletePreviousChar();
            }

            const auto is_valid = ValidateInputText(ui->text_edit_osk->toPlainText());

            const auto text_length = static_cast<u32>(ui->text_edit_osk->toPlainText().length());

            ui->label_characters_box->setText(QStringLiteral("%1/%2")
                                                  .arg(text_length)
                                                  .arg(initialize_parameters.max_text_length));

            ui->button_ok->setEnabled(is_valid);
            ui->button_ok_shift->setEnabled(is_valid);
            ui->button_ok_num->setEnabled(is_valid);

            ui->text_edit_osk->setFocus();
        });

        connect(ui->text_edit_osk, &QTextEdit::cursorPositionChanged, [this] {
            const auto new_cursor_position = ui->text_edit_osk->textCursor().position();

            ui->button_backspace->setEnabled(initialize_parameters.enable_backspace_button &&
                                             new_cursor_position > 0);
            ui->button_backspace_shift->setEnabled(initialize_parameters.enable_backspace_button &&
                                                   new_cursor_position > 0);
            ui->button_backspace_num->setEnabled(initialize_parameters.enable_backspace_button &&
                                                 new_cursor_position > 0);

            ui->text_edit_osk->setFocus();
        });

        ui->text_edit_osk->setPlaceholderText(
            QString::fromStdU16String(initialize_parameters.guide_text));
        ui->text_edit_osk->setText(QString::fromStdU16String(initialize_parameters.initial_text));
        ui->text_edit_osk->moveCursor(initialize_parameters.initial_cursor_position == 0
                                          ? QTextCursor::Start
                                          : QTextCursor::End);

        ui->label_characters_box->setText(QStringLiteral("%1/%2")
                                              .arg(initialize_parameters.initial_text.size())
                                              .arg(initialize_parameters.max_text_length));
        break;
    }
    }
}

void QtSoftwareKeyboardDialog::SetControllerImage() {
    const auto* handheld = system.HIDCore().GetEmulatedController(Core::HID::NpadIdType::Handheld);
    const auto* player_1 = system.HIDCore().GetEmulatedController(Core::HID::NpadIdType::Player1);
    const auto controller_type =
        handheld->IsConnected() ? handheld->GetNpadStyleIndex() : player_1->GetNpadStyleIndex();

    switch (controller_type) {
    case Core::HID::NpadStyleIndex::Fullkey:
    case Core::HID::NpadStyleIndex::GameCube:
        ui->icon_controller->setStyleSheet(
            QStringLiteral("image: url(:/overlay%1/controller_pro.png);").arg(theme()));
        ui->icon_controller_shift->setStyleSheet(
            QStringLiteral("image: url(:/overlay%1/controller_pro.png);").arg(theme()));
        ui->icon_controller_num->setStyleSheet(
            QStringLiteral("image: url(:/overlay%1/controller_pro.png);").arg(theme()));
        break;
    case Core::HID::NpadStyleIndex::JoyconDual:
        ui->icon_controller->setStyleSheet(
            QStringLiteral("image: url(:/overlay%1/controller_dual_joycon.png);").arg(theme()));
        ui->icon_controller_shift->setStyleSheet(
            QStringLiteral("image: url(:/overlay%1/controller_dual_joycon.png);").arg(theme()));
        ui->icon_controller_num->setStyleSheet(
            QStringLiteral("image: url(:/overlay%1/controller_dual_joycon.png);").arg(theme()));
        break;
    case Core::HID::NpadStyleIndex::JoyconLeft:
        ui->icon_controller->setStyleSheet(
            QStringLiteral("image: url(:/overlay%1/controller_single_joycon_left.png);")
                .arg(theme()));
        ui->icon_controller_shift->setStyleSheet(
            QStringLiteral("image: url(:/overlay%1/controller_single_joycon_left.png);")
                .arg(theme()));
        ui->icon_controller_num->setStyleSheet(
            QStringLiteral("image: url(:/overlay%1/controller_single_joycon_left.png);")
                .arg(theme()));
        break;
    case Core::HID::NpadStyleIndex::JoyconRight:
        ui->icon_controller->setStyleSheet(
            QStringLiteral("image: url(:/overlay%1/controller_single_joycon_right.png);")
                .arg(theme()));
        ui->icon_controller_shift->setStyleSheet(
            QStringLiteral("image: url(:/overlay%1/controller_single_joycon_right.png);")
                .arg(theme()));
        ui->icon_controller_num->setStyleSheet(
            QStringLiteral("image: url(:/overlay%1/controller_single_joycon_right.png);")
                .arg(theme()));
        break;
    case Core::HID::NpadStyleIndex::Handheld:
        ui->icon_controller->setStyleSheet(
            QStringLiteral("image: url(:/overlay%1/controller_handheld.png);").arg(theme()));
        ui->icon_controller_shift->setStyleSheet(
            QStringLiteral("image: url(:/overlay%1/controller_handheld.png);").arg(theme()));
        ui->icon_controller_num->setStyleSheet(
            QStringLiteral("image: url(:/overlay%1/controller_handheld.png);").arg(theme()));
        break;
    default:
        break;
    }
}

void QtSoftwareKeyboardDialog::SetButtonImages() {
    QString button_backspace_stylesheet =
        QStringLiteral("QPushButton {"
                       " image: url(:/overlay%1/osk_button_B.png);"
                       " image-position: right;"
                       " qproperty-icon: url(:/overlay%1/osk_button_backspace.png); }"
                       "QPushButton:disabled { image: url(:/overlay%1/osk_button_B_disabled.png);}")
            .arg(theme());
    ui->button_backspace->setStyleSheet(button_backspace_stylesheet);
    ui->button_backspace_shift->setStyleSheet(button_backspace_stylesheet);
    ui->button_backspace_shift->setIconSize(ui->button_backspace->iconSize());
    ui->button_backspace_num->setStyleSheet(button_backspace_stylesheet);

    QString button_space_stylesheet =
        QStringLiteral("QPushButton {"
                       " image: url(:/overlay%1/osk_button_Y.png);"
                       " image-position: right;"
                       " qproperty-icon: url(:/overlay%1/osk_button_space.png); }"
                       "QPushButton:disabled { image: url(:/overlay%1/osk_button_Y_disabled.png);}")
            .arg(theme());
    ui->button_space->setStyleSheet(button_space_stylesheet);
    ui->button_space_shift->setStyleSheet(button_space_stylesheet);

    QString button_ok_stylesheet =
        QStringLiteral(
            "QPushButton { image: url(:/overlay%1/osk_button_plus.png); }"
            "QPushButton:disabled { image: url(:/overlay%1/osk_button_plus_disabled.png); }")
            .arg(theme());
    ui->button_ok->setStyleSheet(button_ok_stylesheet);
    ui->button_ok_shift->setStyleSheet(button_ok_stylesheet);
    ui->button_ok_num->setStyleSheet(button_ok_stylesheet);

    QString button_shift_stylesheet =
        QStringLiteral("image: url(:/overlay/osk_button_shift_lock_off.png);"
                       "image-position: left;"
                       "qproperty-icon: url(:/overlay%1/osk_button_shift.png);")
            .arg(theme());
    ui->button_shift->setStyleSheet(button_shift_stylesheet);

    QString button_shift_shift_stylesheet =
        QStringLiteral("image: url(:/overlay/osk_button_shift_lock_off.png);"
                       "image-position: left;"
                       "qproperty-icon: url(:/overlay%1/osk_button_shift_on.png);")
            .arg(theme());
    ui->button_shift_shift->setStyleSheet(button_shift_shift_stylesheet);
    ui->button_shift_shift->setIconSize(ui->button_shift->iconSize());

    QString L_stylesheet = QStringLiteral("image: url(:/overlay%1/button_L.png);").arg(theme());
    ui->button_L->setStyleSheet(L_stylesheet);
    ui->button_L_shift->setStyleSheet(L_stylesheet);
    ui->button_L_num->setStyleSheet(L_stylesheet);

    QString R_stylesheet = QStringLiteral("image: url(:/overlay%1/button_R.png);").arg(theme());
    ui->button_R->setStyleSheet(R_stylesheet);
    ui->button_R_shift->setStyleSheet(R_stylesheet);
    ui->button_R_num->setStyleSheet(R_stylesheet);

    QString arrow_left_stylesheet =
        QStringLiteral("image: url(:/overlay%1/arrow_left.png);").arg(theme());
    ui->arrow_left->setStyleSheet(arrow_left_stylesheet);
    ui->arrow_left_shift->setStyleSheet(arrow_left_stylesheet);
    ui->arrow_left_num->setStyleSheet(arrow_left_stylesheet);

    QString arrow_right_stylesheet =
        QStringLiteral("image: url(:/overlay%1/arrow_right.png);").arg(theme());
    ui->arrow_right->setStyleSheet(arrow_right_stylesheet);
    ui->arrow_right_shift->setStyleSheet(arrow_right_stylesheet);
    ui->arrow_right_num->setStyleSheet(arrow_right_stylesheet);

    QString button_press_stick_stylesheet =
        QStringLiteral("image: url(:/overlay%1/button_press_stick.png);").arg(theme());
    ui->button_press_stick->setStyleSheet(button_press_stick_stylesheet);
    ui->button_press_stick_shift->setStyleSheet(button_press_stick_stylesheet);

    QString button_X_stylesheet =
        QStringLiteral("image: url(:/overlay%1/button_X.png);").arg(theme());
    ui->button_X->setStyleSheet(button_X_stylesheet);
    ui->button_X_shift->setStyleSheet(button_X_stylesheet);
    ui->button_X_num->setStyleSheet(button_X_stylesheet);

    QString button_A_stylesheet =
        QStringLiteral("image: url(:/overlay%1/button_A.png);").arg(theme());
    ui->button_A->setStyleSheet(button_A_stylesheet);
    ui->button_A_shift->setStyleSheet(button_A_stylesheet);
    ui->button_A_num->setStyleSheet(button_A_stylesheet);
}

void QtSoftwareKeyboardDialog::DisableKeyboardButtons() {
    switch (bottom_osk_index) {
    case BottomOSKIndex::LowerCase:
    case BottomOSKIndex::UpperCase:
    default: {
        for (const auto& keys : keyboard_buttons) {
            for (const auto& rows : keys) {
                for (auto* button : rows) {
                    if (!button) {
                        continue;
                    }

                    button->setEnabled(true);
                }
            }
        }

        const auto& key_disable_flags = initialize_parameters.key_disable_flags;

        ui->button_space->setDisabled(key_disable_flags.space);
        ui->button_space_shift->setDisabled(key_disable_flags.space);

        ui->button_at->setDisabled(key_disable_flags.at || key_disable_flags.username);

        ui->button_percent->setDisabled(key_disable_flags.percent || key_disable_flags.username);

        ui->button_slash->setDisabled(key_disable_flags.slash);

        ui->button_1->setDisabled(key_disable_flags.numbers);
        ui->button_2->setDisabled(key_disable_flags.numbers);
        ui->button_3->setDisabled(key_disable_flags.numbers);
        ui->button_4->setDisabled(key_disable_flags.numbers);
        ui->button_5->setDisabled(key_disable_flags.numbers);
        ui->button_6->setDisabled(key_disable_flags.numbers);
        ui->button_7->setDisabled(key_disable_flags.numbers);
        ui->button_8->setDisabled(key_disable_flags.numbers);
        ui->button_9->setDisabled(key_disable_flags.numbers);
        ui->button_0->setDisabled(key_disable_flags.numbers);

        ui->button_return->setEnabled(initialize_parameters.enable_return_button);
        ui->button_return_shift->setEnabled(initialize_parameters.enable_return_button);
        break;
    }
    case BottomOSKIndex::NumberPad: {
        for (const auto& rows : numberpad_buttons) {
            for (auto* button : rows) {
                if (!button) {
                    continue;
                }

                button->setEnabled(true);
            }
        }

        const auto enable_left_optional = initialize_parameters.left_optional_symbol_key != '\0';
        const auto enable_right_optional = initialize_parameters.right_optional_symbol_key != '\0';

        ui->button_left_optional_num->setEnabled(enable_left_optional);
        ui->button_left_optional_num->setVisible(enable_left_optional);

        ui->button_right_optional_num->setEnabled(enable_right_optional);
        ui->button_right_optional_num->setVisible(enable_right_optional);
        break;
    }
    }
}

void QtSoftwareKeyboardDialog::SetBackspaceOkEnabled() {
    if (is_inline) {
        ui->button_ok->setEnabled(current_text.size() >= initialize_parameters.min_text_length);
        ui->button_ok_shift->setEnabled(current_text.size() >=
                                        initialize_parameters.min_text_length);
        ui->button_ok_num->setEnabled(current_text.size() >= initialize_parameters.min_text_length);

        ui->button_backspace->setEnabled(initialize_parameters.enable_backspace_button &&
                                         cursor_position > 0);
        ui->button_backspace_shift->setEnabled(initialize_parameters.enable_backspace_button &&
                                               cursor_position > 0);
        ui->button_backspace_num->setEnabled(initialize_parameters.enable_backspace_button &&
                                             cursor_position > 0);
    } else {
        const auto text_length = [this] {
            if (ui->topOSK->currentIndex() == 1) {
                return static_cast<u32>(ui->text_edit_osk->toPlainText().length());
            } else {
                return static_cast<u32>(ui->line_edit_osk->text().length());
            }
        }();

        const auto normal_cursor_position = [this] {
            if (ui->topOSK->currentIndex() == 1) {
                return ui->text_edit_osk->textCursor().position();
            } else {
                return ui->line_edit_osk->cursorPosition();
            }
        }();

        ui->button_ok->setEnabled(text_length >= initialize_parameters.min_text_length);
        ui->button_ok_shift->setEnabled(text_length >= initialize_parameters.min_text_length);
        ui->button_ok_num->setEnabled(text_length >= initialize_parameters.min_text_length);

        ui->button_backspace->setEnabled(initialize_parameters.enable_backspace_button &&
                                         normal_cursor_position > 0);
        ui->button_backspace_shift->setEnabled(initialize_parameters.enable_backspace_button &&
                                               normal_cursor_position > 0);
        ui->button_backspace_num->setEnabled(initialize_parameters.enable_backspace_button &&
                                             normal_cursor_position > 0);
    }
}

bool QtSoftwareKeyboardDialog::ValidateInputText(const QString& input_text) {
    const auto& key_disable_flags = initialize_parameters.key_disable_flags;

    const auto input_text_length = static_cast<u32>(input_text.length());

    if (input_text_length < initialize_parameters.min_text_length ||
        input_text_length > initialize_parameters.max_text_length) {
        return false;
    }

    if (key_disable_flags.space && input_text.contains(QLatin1Char{' '})) {
        return false;
    }

    if ((key_disable_flags.at || key_disable_flags.username) &&
        input_text.contains(QLatin1Char{'@'})) {
        return false;
    }

    if ((key_disable_flags.percent || key_disable_flags.username) &&
        input_text.contains(QLatin1Char{'%'})) {
        return false;
    }

    if (key_disable_flags.slash && input_text.contains(QLatin1Char{'/'})) {
        return false;
    }

    if ((key_disable_flags.backslash || key_disable_flags.username) &&
        input_text.contains(QLatin1Char('\\'))) {
        return false;
    }

    if (key_disable_flags.numbers &&
        std::any_of(input_text.begin(), input_text.end(), [](QChar c) { return c.isDigit(); })) {
        return false;
    }

    if (bottom_osk_index == BottomOSKIndex::NumberPad &&
        std::any_of(input_text.begin(), input_text.end(), [this](QChar c) {
            return !c.isDigit() && c != QChar{initialize_parameters.left_optional_symbol_key} &&
                   c != QChar{initialize_parameters.right_optional_symbol_key};
        })) {
        return false;
    }

    return true;
}

void QtSoftwareKeyboardDialog::ChangeBottomOSKIndex() {
    switch (bottom_osk_index) {
    case BottomOSKIndex::LowerCase:
        bottom_osk_index = BottomOSKIndex::UpperCase;
        ui->bottomOSK->setCurrentIndex(static_cast<int>(bottom_osk_index));

        ui->button_shift_shift->setStyleSheet(
            QStringLiteral("image: url(:/overlay/osk_button_shift_lock_off.png);"));

        ui->button_backspace_shift->setIconSize(ui->button_backspace->iconSize());
        break;
    case BottomOSKIndex::UpperCase:
        if (caps_lock_enabled) {
            caps_lock_enabled = false;

            ui->button_shift_shift->setStyleSheet(
                QStringLiteral("image: url(:/overlay/osk_button_shift_lock_off.png);"
                               "\nimage-position: left;"));

            ui->label_shift_shift->setText(QStringLiteral("Caps Lock"));

            bottom_osk_index = BottomOSKIndex::LowerCase;
            ui->bottomOSK->setCurrentIndex(static_cast<int>(bottom_osk_index));
        } else {
            caps_lock_enabled = true;

            ui->button_shift_shift->setStyleSheet(
                QStringLiteral("image: url(:/overlay/osk_button_shift_lock_on.png);"
                               "\nimage-position: left;"));

            ui->label_shift_shift->setText(QStringLiteral("Caps Lock Off"));
        }
        break;
    case BottomOSKIndex::NumberPad:
    default:
        break;
    }
}

void QtSoftwareKeyboardDialog::NormalKeyboardButtonClicked(QPushButton* button) {
    if (button == ui->button_ampersand) {
        if (ui->topOSK->currentIndex() == 1) {
            ui->text_edit_osk->insertPlainText(QStringLiteral("&"));
        } else {
            ui->line_edit_osk->insert(QStringLiteral("&"));
        }
        return;
    }

    if (button == ui->button_return || button == ui->button_return_shift) {
        if (ui->topOSK->currentIndex() == 1) {
            ui->text_edit_osk->insertPlainText(QStringLiteral("\n"));
        } else {
            ui->line_edit_osk->insert(QStringLiteral("\n"));
        }
        return;
    }

    if (button == ui->button_space || button == ui->button_space_shift) {
        if (ui->topOSK->currentIndex() == 1) {
            ui->text_edit_osk->insertPlainText(QStringLiteral(" "));
        } else {
            ui->line_edit_osk->insert(QStringLiteral(" "));
        }
        return;
    }

    if (button == ui->button_shift || button == ui->button_shift_shift) {
        ChangeBottomOSKIndex();
        return;
    }

    if (button == ui->button_backspace || button == ui->button_backspace_shift ||
        button == ui->button_backspace_num) {
        if (ui->topOSK->currentIndex() == 1) {
            auto text_cursor = ui->text_edit_osk->textCursor();
            ui->text_edit_osk->setTextCursor(text_cursor);
            text_cursor.deletePreviousChar();
        } else {
            ui->line_edit_osk->backspace();
        }
        return;
    }

    if (button == ui->button_ok || button == ui->button_ok_shift || button == ui->button_ok_num) {
        const auto text = ui->topOSK->currentIndex() == 1 ? ui->text_edit_osk->toPlainText()
                                                          : ui->line_edit_osk->text();
        auto text_str = Common::U16StringFromBuffer(text.utf16(), text.size());

        emit SubmitNormalText(SwkbdResult::Ok, std::move(text_str));
        return;
    }

    if (ui->topOSK->currentIndex() == 1) {
        ui->text_edit_osk->insertPlainText(button->text());
    } else {
        ui->line_edit_osk->insert(button->text());
    }

    // Revert the keyboard to lowercase if the shift key is active.
    if (bottom_osk_index == BottomOSKIndex::UpperCase && !caps_lock_enabled) {
        // This is set to true since ChangeBottomOSKIndex will change bottom_osk_index to LowerCase
        // if bottom_osk_index is UpperCase and caps_lock_enabled is true.
        caps_lock_enabled = true;
        ChangeBottomOSKIndex();
    }
}

void QtSoftwareKeyboardDialog::InlineKeyboardButtonClicked(QPushButton* button) {
    if (!button->isEnabled()) {
        return;
    }

    if (button == ui->button_ampersand) {
        InlineTextInsertString(u"&");
        return;
    }

    if (button == ui->button_return || button == ui->button_return_shift) {
        InlineTextInsertString(u"\n");
        return;
    }

    if (button == ui->button_space || button == ui->button_space_shift) {
        InlineTextInsertString(u" ");
        return;
    }

    if (button == ui->button_shift || button == ui->button_shift_shift) {
        ChangeBottomOSKIndex();
        return;
    }

    if (button == ui->button_backspace || button == ui->button_backspace_shift ||
        button == ui->button_backspace_num) {
        if (cursor_position <= 0 || current_text.empty()) {
            cursor_position = 0;
            return;
        }

        --cursor_position;

        current_text.erase(cursor_position, 1);

        SetBackspaceOkEnabled();

        emit SubmitInlineText(SwkbdReplyType::ChangedString, current_text, cursor_position);
        return;
    }

    if (button == ui->button_ok || button == ui->button_ok_shift || button == ui->button_ok_num) {
        emit SubmitInlineText(SwkbdReplyType::DecidedEnter, current_text, cursor_position);
        return;
    }

    const auto button_text = button->text();
    InlineTextInsertString(Common::U16StringFromBuffer(button_text.utf16(), button_text.size()));

    // Revert the keyboard to lowercase if the shift key is active.
    if (bottom_osk_index == BottomOSKIndex::UpperCase && !caps_lock_enabled) {
        // This is set to true since ChangeBottomOSKIndex will change bottom_osk_index to LowerCase
        // if bottom_osk_index is UpperCase and caps_lock_enabled is true.
        caps_lock_enabled = true;
        ChangeBottomOSKIndex();
    }
}

void QtSoftwareKeyboardDialog::InlineTextInsertString(std::u16string_view string) {
    if ((current_text.size() + string.size()) > initialize_parameters.max_text_length) {
        return;
    }

    current_text.insert(cursor_position, string);

    cursor_position += static_cast<s32>(string.size());

    SetBackspaceOkEnabled();

    emit SubmitInlineText(SwkbdReplyType::ChangedString, current_text, cursor_position);
}

void QtSoftwareKeyboardDialog::SetupMouseHover() {
    // setFocus() has a bug where continuously changing focus will cause the focus UI to
    // mysteriously disappear. A workaround we have found is using the mouse to hover over
    // the buttons to act in place of the button focus. As a result, we will have to set
    // a blank cursor when hovering over all the buttons and set a no focus policy so the
    // buttons do not stay in focus in addition to the mouse hover.
    for (auto* button : all_buttons) {
        button->setCursor(QCursor(Qt::BlankCursor));
        button->setFocusPolicy(Qt::NoFocus);
    }
}

template <Core::HID::NpadButton... T>
void QtSoftwareKeyboardDialog::HandleButtonPressedOnce() {
    const auto f = [this](Core::HID::NpadButton button) {
        if (input_interpreter->IsButtonPressedOnce(button)) {
            TranslateButtonPress(button);
        }
    };

    (f(T), ...);
}

template <Core::HID::NpadButton... T>
void QtSoftwareKeyboardDialog::HandleButtonHold() {
    const auto f = [this](Core::HID::NpadButton button) {
        if (input_interpreter->IsButtonHeld(button)) {
            TranslateButtonPress(button);
        }
    };

    (f(T), ...);
}

void QtSoftwareKeyboardDialog::TranslateButtonPress(Core::HID::NpadButton button) {
    switch (button) {
    case Core::HID::NpadButton::A:
        switch (bottom_osk_index) {
        case BottomOSKIndex::LowerCase:
        case BottomOSKIndex::UpperCase:
            keyboard_buttons[static_cast<std::size_t>(bottom_osk_index)][row][column]->click();
            break;
        case BottomOSKIndex::NumberPad:
            numberpad_buttons[row][column]->click();
            break;
        default:
            break;
        }
        break;
    case Core::HID::NpadButton::B:
        switch (bottom_osk_index) {
        case BottomOSKIndex::LowerCase:
            ui->button_backspace->click();
            break;
        case BottomOSKIndex::UpperCase:
            ui->button_backspace_shift->click();
            break;
        case BottomOSKIndex::NumberPad:
            ui->button_backspace_num->click();
            break;
        default:
            break;
        }
        break;
    case Core::HID::NpadButton::X:
        if (is_inline) {
            emit SubmitInlineText(SwkbdReplyType::DecidedCancel, current_text, cursor_position);
        } else {
            const auto text = ui->topOSK->currentIndex() == 1 ? ui->text_edit_osk->toPlainText()
                                                              : ui->line_edit_osk->text();
            auto text_str = Common::U16StringFromBuffer(text.utf16(), text.size());

            emit SubmitNormalText(SwkbdResult::Cancel, std::move(text_str));
        }
        break;
    case Core::HID::NpadButton::Y:
        switch (bottom_osk_index) {
        case BottomOSKIndex::LowerCase:
            ui->button_space->click();
            break;
        case BottomOSKIndex::UpperCase:
            ui->button_space_shift->click();
            break;
        case BottomOSKIndex::NumberPad:
        default:
            break;
        }
        break;
    case Core::HID::NpadButton::StickL:
    case Core::HID::NpadButton::StickR:
        switch (bottom_osk_index) {
        case BottomOSKIndex::LowerCase:
            ui->button_shift->click();
            break;
        case BottomOSKIndex::UpperCase:
            ui->button_shift_shift->click();
            break;
        case BottomOSKIndex::NumberPad:
        default:
            break;
        }
        break;
    case Core::HID::NpadButton::L:
        MoveTextCursorDirection(Direction::Left);
        break;
    case Core::HID::NpadButton::R:
        MoveTextCursorDirection(Direction::Right);
        break;
    case Core::HID::NpadButton::Plus:
        switch (bottom_osk_index) {
        case BottomOSKIndex::LowerCase:
            ui->button_ok->click();
            break;
        case BottomOSKIndex::UpperCase:
            ui->button_ok_shift->click();
            break;
        case BottomOSKIndex::NumberPad:
            ui->button_ok_num->click();
            break;
        default:
            break;
        }
        break;
    case Core::HID::NpadButton::Left:
    case Core::HID::NpadButton::StickLLeft:
    case Core::HID::NpadButton::StickRLeft:
        MoveButtonDirection(Direction::Left);
        break;
    case Core::HID::NpadButton::Up:
    case Core::HID::NpadButton::StickLUp:
    case Core::HID::NpadButton::StickRUp:
        MoveButtonDirection(Direction::Up);
        break;
    case Core::HID::NpadButton::Right:
    case Core::HID::NpadButton::StickLRight:
    case Core::HID::NpadButton::StickRRight:
        MoveButtonDirection(Direction::Right);
        break;
    case Core::HID::NpadButton::Down:
    case Core::HID::NpadButton::StickLDown:
    case Core::HID::NpadButton::StickRDown:
        MoveButtonDirection(Direction::Down);
        break;
    default:
        break;
    }
}

void QtSoftwareKeyboardDialog::MoveButtonDirection(Direction direction) {
    // Changes the row or column index depending on the direction.
    auto move_direction = [this, direction](std::size_t max_rows, std::size_t max_columns) {
        switch (direction) {
        case Direction::Left:
            column = (column + max_columns - 1) % max_columns;
            break;
        case Direction::Up:
            row = (row + max_rows - 1) % max_rows;
            break;
        case Direction::Right:
            column = (column + 1) % max_columns;
            break;
        case Direction::Down:
            row = (row + 1) % max_rows;
            break;
        default:
            break;
        }
    };

    // Store the initial row and column.
    const auto initial_row = row;
    const auto initial_column = column;

    switch (bottom_osk_index) {
    case BottomOSKIndex::LowerCase:
    case BottomOSKIndex::UpperCase: {
        const auto index = static_cast<std::size_t>(bottom_osk_index);

        const auto* const prev_button = keyboard_buttons[index][row][column];
        move_direction(NUM_ROWS_NORMAL, NUM_COLUMNS_NORMAL);
        auto* curr_button = keyboard_buttons[index][row][column];

        while (!curr_button || !curr_button->isEnabled() || curr_button == prev_button) {
            // If we returned back to where we started from, break the loop.
            if (row == initial_row && column == initial_column) {
                break;
            }

            move_direction(NUM_ROWS_NORMAL, NUM_COLUMNS_NORMAL);
            curr_button = keyboard_buttons[index][row][column];
        }

        // This is a workaround for setFocus() randomly not showing focus in the UI
        QCursor::setPos(curr_button->mapToGlobal(curr_button->rect().center()));
        break;
    }
    case BottomOSKIndex::NumberPad: {
        const auto* const prev_button = numberpad_buttons[row][column];
        move_direction(NUM_ROWS_NUMPAD, NUM_COLUMNS_NUMPAD);
        auto* curr_button = numberpad_buttons[row][column];

        while (!curr_button || !curr_button->isEnabled() || curr_button == prev_button) {
            // If we returned back to where we started from, break the loop.
            if (row == initial_row && column == initial_column) {
                break;
            }

            move_direction(NUM_ROWS_NUMPAD, NUM_COLUMNS_NUMPAD);
            curr_button = numberpad_buttons[row][column];
        }

        // This is a workaround for setFocus() randomly not showing focus in the UI
        QCursor::setPos(curr_button->mapToGlobal(curr_button->rect().center()));
        break;
    }
    default:
        break;
    }
}

void QtSoftwareKeyboardDialog::MoveTextCursorDirection(Direction direction) {
    switch (direction) {
    case Direction::Left:
        if (is_inline) {
            if (cursor_position <= 0) {
                cursor_position = 0;
            } else {
                --cursor_position;
                emit SubmitInlineText(SwkbdReplyType::MovedCursor, current_text, cursor_position);
            }
        } else {
            if (ui->topOSK->currentIndex() == 1) {
                ui->text_edit_osk->moveCursor(QTextCursor::Left);
            } else {
                ui->line_edit_osk->setCursorPosition(ui->line_edit_osk->cursorPosition() - 1);
            }
        }
        break;
    case Direction::Right:
        if (is_inline) {
            if (cursor_position >= static_cast<s32>(current_text.size())) {
                cursor_position = static_cast<s32>(current_text.size());
            } else {
                ++cursor_position;
                emit SubmitInlineText(SwkbdReplyType::MovedCursor, current_text, cursor_position);
            }
        } else {
            if (ui->topOSK->currentIndex() == 1) {
                ui->text_edit_osk->moveCursor(QTextCursor::Right);
            } else {
                ui->line_edit_osk->setCursorPosition(ui->line_edit_osk->cursorPosition() + 1);
            }
        }
        break;
    default:
        break;
    }
}

void QtSoftwareKeyboardDialog::StartInputThread() {
    if (input_thread_running) {
        return;
    }

    input_thread_running = true;

    input_thread = std::thread(&QtSoftwareKeyboardDialog::InputThread, this);
}

void QtSoftwareKeyboardDialog::StopInputThread() {
    input_thread_running = false;

    if (input_thread.joinable()) {
        input_thread.join();
    }

    if (input_interpreter) {
        input_interpreter->ResetButtonStates();
    }
}

void QtSoftwareKeyboardDialog::InputThread() {
    while (input_thread_running) {
        input_interpreter->PollInput();

        HandleButtonPressedOnce<
            Core::HID::NpadButton::A, Core::HID::NpadButton::B, Core::HID::NpadButton::X,
            Core::HID::NpadButton::Y, Core::HID::NpadButton::StickL, Core::HID::NpadButton::StickR,
            Core::HID::NpadButton::L, Core::HID::NpadButton::R, Core::HID::NpadButton::Plus,
            Core::HID::NpadButton::Left, Core::HID::NpadButton::Up, Core::HID::NpadButton::Right,
            Core::HID::NpadButton::Down, Core::HID::NpadButton::StickLLeft,
            Core::HID::NpadButton::StickLUp, Core::HID::NpadButton::StickLRight,
            Core::HID::NpadButton::StickLDown, Core::HID::NpadButton::StickRLeft,
            Core::HID::NpadButton::StickRUp, Core::HID::NpadButton::StickRRight,
            Core::HID::NpadButton::StickRDown>();

        HandleButtonHold<Core::HID::NpadButton::B, Core::HID::NpadButton::L,
                         Core::HID::NpadButton::R, Core::HID::NpadButton::Left,
                         Core::HID::NpadButton::Up, Core::HID::NpadButton::Right,
                         Core::HID::NpadButton::Down, Core::HID::NpadButton::StickLLeft,
                         Core::HID::NpadButton::StickLUp, Core::HID::NpadButton::StickLRight,
                         Core::HID::NpadButton::StickLDown, Core::HID::NpadButton::StickRLeft,
                         Core::HID::NpadButton::StickRUp, Core::HID::NpadButton::StickRRight,
                         Core::HID::NpadButton::StickRDown>();

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

QtSoftwareKeyboard::QtSoftwareKeyboard(GMainWindow& main_window) {
    connect(this, &QtSoftwareKeyboard::MainWindowInitializeKeyboard, &main_window,
            &GMainWindow::SoftwareKeyboardInitialize, Qt::QueuedConnection);
    connect(this, &QtSoftwareKeyboard::MainWindowShowNormalKeyboard, &main_window,
            &GMainWindow::SoftwareKeyboardShowNormal, Qt::QueuedConnection);
    connect(this, &QtSoftwareKeyboard::MainWindowShowTextCheckDialog, &main_window,
            &GMainWindow::SoftwareKeyboardShowTextCheck, Qt::QueuedConnection);
    connect(this, &QtSoftwareKeyboard::MainWindowShowInlineKeyboard, &main_window,
            &GMainWindow::SoftwareKeyboardShowInline, Qt::QueuedConnection);
    connect(this, &QtSoftwareKeyboard::MainWindowHideInlineKeyboard, &main_window,
            &GMainWindow::SoftwareKeyboardHideInline, Qt::QueuedConnection);
    connect(this, &QtSoftwareKeyboard::MainWindowInlineTextChanged, &main_window,
            &GMainWindow::SoftwareKeyboardInlineTextChanged, Qt::QueuedConnection);
    connect(this, &QtSoftwareKeyboard::MainWindowExitKeyboard, &main_window,
            &GMainWindow::SoftwareKeyboardExit, Qt::QueuedConnection);
    connect(&main_window, &GMainWindow::SoftwareKeyboardSubmitNormalText, this,
            &QtSoftwareKeyboard::SubmitNormalText, Qt::QueuedConnection);
    connect(&main_window, &GMainWindow::SoftwareKeyboardSubmitInlineText, this,
            &QtSoftwareKeyboard::SubmitInlineText, Qt::QueuedConnection);
}

QtSoftwareKeyboard::~QtSoftwareKeyboard() = default;

void QtSoftwareKeyboard::InitializeKeyboard(
    bool is_inline, Core::Frontend::KeyboardInitializeParameters initialize_parameters,
    SubmitNormalCallback submit_normal_callback_, SubmitInlineCallback submit_inline_callback_) {
    if (is_inline) {
        submit_inline_callback = std::move(submit_inline_callback_);
    } else {
        submit_normal_callback = std::move(submit_normal_callback_);
    }

    LOG_INFO(Service_AM,
             "\nKeyboardInitializeParameters:"
             "\nok_text={}"
             "\nheader_text={}"
             "\nsub_text={}"
             "\nguide_text={}"
             "\ninitial_text={}"
             "\nmax_text_length={}"
             "\nmin_text_length={}"
             "\ninitial_cursor_position={}"
             "\ntype={}"
             "\npassword_mode={}"
             "\ntext_draw_type={}"
             "\nkey_disable_flags={}"
             "\nuse_blur_background={}"
             "\nenable_backspace_button={}"
             "\nenable_return_button={}"
             "\ndisable_cancel_button={}",
             Common::UTF16ToUTF8(initialize_parameters.ok_text),
             Common::UTF16ToUTF8(initialize_parameters.header_text),
             Common::UTF16ToUTF8(initialize_parameters.sub_text),
             Common::UTF16ToUTF8(initialize_parameters.guide_text),
             Common::UTF16ToUTF8(initialize_parameters.initial_text),
             initialize_parameters.max_text_length, initialize_parameters.min_text_length,
             initialize_parameters.initial_cursor_position, initialize_parameters.type,
             initialize_parameters.password_mode, initialize_parameters.text_draw_type,
             initialize_parameters.key_disable_flags.raw, initialize_parameters.use_blur_background,
             initialize_parameters.enable_backspace_button,
             initialize_parameters.enable_return_button,
             initialize_parameters.disable_cancel_button);

    emit MainWindowInitializeKeyboard(is_inline, std::move(initialize_parameters));
}

void QtSoftwareKeyboard::ShowNormalKeyboard() const {
    emit MainWindowShowNormalKeyboard();
}

void QtSoftwareKeyboard::ShowTextCheckDialog(
    Service::AM::Frontend::SwkbdTextCheckResult text_check_result,
    std::u16string text_check_message) const {
    emit MainWindowShowTextCheckDialog(text_check_result, std::move(text_check_message));
}

void QtSoftwareKeyboard::ShowInlineKeyboard(
    Core::Frontend::InlineAppearParameters appear_parameters) const {
    LOG_INFO(Service_AM,
             "\nInlineAppearParameters:"
             "\nmax_text_length={}"
             "\nmin_text_length={}"
             "\nkey_top_scale_x={}"
             "\nkey_top_scale_y={}"
             "\nkey_top_translate_x={}"
             "\nkey_top_translate_y={}"
             "\ntype={}"
             "\nkey_disable_flags={}"
             "\nkey_top_as_floating={}"
             "\nenable_backspace_button={}"
             "\nenable_return_button={}"
             "\ndisable_cancel_button={}",
             appear_parameters.max_text_length, appear_parameters.min_text_length,
             appear_parameters.key_top_scale_x, appear_parameters.key_top_scale_y,
             appear_parameters.key_top_translate_x, appear_parameters.key_top_translate_y,
             appear_parameters.type, appear_parameters.key_disable_flags.raw,
             appear_parameters.key_top_as_floating, appear_parameters.enable_backspace_button,
             appear_parameters.enable_return_button, appear_parameters.disable_cancel_button);

    emit MainWindowShowInlineKeyboard(std::move(appear_parameters));
}

void QtSoftwareKeyboard::HideInlineKeyboard() const {
    emit MainWindowHideInlineKeyboard();
}

void QtSoftwareKeyboard::InlineTextChanged(
    Core::Frontend::InlineTextParameters text_parameters) const {
    LOG_INFO(Service_AM,
             "\nInlineTextParameters:"
             "\ninput_text={}"
             "\ncursor_position={}",
             Common::UTF16ToUTF8(text_parameters.input_text), text_parameters.cursor_position);

    emit MainWindowInlineTextChanged(std::move(text_parameters));
}

void QtSoftwareKeyboard::ExitKeyboard() const {
    emit MainWindowExitKeyboard();
}

void QtSoftwareKeyboard::SubmitNormalText(Service::AM::Frontend::SwkbdResult result,
                                          std::u16string submitted_text, bool confirmed) const {
    submit_normal_callback(result, submitted_text, confirmed);
}

void QtSoftwareKeyboard::SubmitInlineText(Service::AM::Frontend::SwkbdReplyType reply_type,
                                          std::u16string submitted_text,
                                          s32 cursor_position) const {
    submit_inline_callback(reply_type, submitted_text, cursor_position);
}
