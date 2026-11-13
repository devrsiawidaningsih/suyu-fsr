// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <functional>
#include <iosfwd>
#include <iterator>
#include <string>
#include <tuple>
#include <typeinfo>
#include <utility>
#include <vector>
#include <QBoxLayout>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QStringLiteral>
#include <QtCore/qobjectdefs.h>
#include <qabstractbutton.h>
#include <qboxlayout.h>
#include <qcombobox.h>
#include <qcoreevent.h>
#include <qglobal.h>
#include <qgridlayout.h>
#include <vulkan/vulkan_core.h>

#include "common/common_types.h"
#include "common/dynamic_library.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "common/settings_enums.h"
#include "core/core.h"
#include "suyu/configuration/configuration_shared.h"
#include "suyu/configuration/configure_graphics.h"
#include "suyu/configuration/shared_widget.h"
#include "suyu/qt_common.h"
#include "suyu/uisettings.h"
#include "suyu/vk_device_info.h"
#include "ui_configure_graphics.h"

static const std::vector<VkPresentModeKHR> default_present_modes{VK_PRESENT_MODE_IMMEDIATE_KHR,
                                                                 VK_PRESENT_MODE_FIFO_KHR};

// Converts a setting to a present mode (or vice versa)
static constexpr VkPresentModeKHR VSyncSettingToMode(Settings::VSyncMode mode) {
    switch (mode) {
    case Settings::VSyncMode::Immediate:
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    case Settings::VSyncMode::Mailbox:
        return VK_PRESENT_MODE_MAILBOX_KHR;
    case Settings::VSyncMode::Fifo:
        return VK_PRESENT_MODE_FIFO_KHR;
    case Settings::VSyncMode::FifoRelaxed:
        return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
    default:
        return VK_PRESENT_MODE_FIFO_KHR;
    }
}

static constexpr bool IsOpenGL(Settings::RendererBackend backend) {
    return backend == Settings::RendererBackend::OpenGL_GLSL ||
           backend == Settings::RendererBackend::OpenGL_GLASM ||
           backend == Settings::RendererBackend::OpenGL_SPIRV;
}

static constexpr Settings::VSyncMode PresentModeToSetting(VkPresentModeKHR mode) {
    switch (mode) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return Settings::VSyncMode::Immediate;
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return Settings::VSyncMode::Mailbox;
    case VK_PRESENT_MODE_FIFO_KHR:
        return Settings::VSyncMode::Fifo;
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        return Settings::VSyncMode::FifoRelaxed;
    default:
        return Settings::VSyncMode::Fifo;
    }
}

ConfigureGraphics::ConfigureGraphics(
    const Core::System& system_, std::vector<VkDeviceInfo::Record>& records_,
    const std::function<void()>& expose_compute_option_,
    const std::function<void(Settings::AspectRatio, Settings::ResolutionSetup)>&
        update_aspect_ratio_,
    std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group_,
    const ConfigurationShared::Builder& builder, QWidget* parent)
    : ConfigurationShared::Tab(group_, parent), ui{std::make_unique<Ui::ConfigureGraphics>()},
      records{records_}, expose_compute_option{expose_compute_option_},
      update_aspect_ratio{update_aspect_ratio_}, system{system_},
      combobox_translations{builder.ComboboxTranslations()} {
    vulkan_device = Settings::values.vulkan_device.GetValue();
    RetrieveVulkanDevices();

    ui->setupUi(this);

    Setup(builder);

    for (const auto& device : vulkan_devices) {
        vulkan_device_combobox->addItem(device);
    }

    UpdateBackgroundColorButton(QColor::fromRgb(Settings::values.bg_red.GetValue(),
                                                Settings::values.bg_green.GetValue(),
                                                Settings::values.bg_blue.GetValue()));
    UpdateAPILayout();
    PopulateVSyncModeSelection(false); //< must happen after UpdateAPILayout

    // VSync setting needs to be determined after populating the VSync combobox
    const auto vsync_mode_setting = Settings::values.vsync_mode.GetValue();
    const auto vsync_mode = VSyncSettingToMode(vsync_mode_setting);
    int index{};
    for (const auto mode : vsync_mode_combobox_enum_map) {
        if (mode == vsync_mode) {
            break;
        }
        index++;
    }
    if (static_cast<unsigned long>(index) < vsync_mode_combobox_enum_map.size()) {
        vsync_mode_combobox->setCurrentIndex(index);
    }

    connect(api_combobox, qOverload<int>(&QComboBox::activated), this, [this] {
        UpdateAPILayout();
        PopulateVSyncModeSelection(false);
    });
    connect(vulkan_device_combobox, qOverload<int>(&QComboBox::activated), this,
            [this](int device) {
                UpdateDeviceSelection(device);
                PopulateVSyncModeSelection(false);
            });
    connect(ui->bg_button, &QPushButton::clicked, this, [this] {
        const QColor new_bg_color = QColorDialog::getColor(bg_color);
        if (!new_bg_color.isValid()) {
            return;
        }
        UpdateBackgroundColorButton(new_bg_color);
    });

    const auto& update_screenshot_info = [this, &builder]() {
        const auto& combobox_enumerations = builder.ComboboxTranslations().at(
            Settings::EnumMetadata<Settings::AspectRatio>::Index());
        const auto ratio_index = aspect_ratio_combobox->currentIndex();
        const auto ratio =
            static_cast<Settings::AspectRatio>(combobox_enumerations[ratio_index].first);

        const auto& combobox_enumerations_resolution = builder.ComboboxTranslations().at(
            Settings::EnumMetadata<Settings::ResolutionSetup>::Index());
        const auto res_index = resolution_combobox->currentIndex();
        const auto setup = static_cast<Settings::ResolutionSetup>(
            combobox_enumerations_resolution[res_index].first);

        update_aspect_ratio(ratio, setup);
    };

    connect(aspect_ratio_combobox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            update_screenshot_info);
    connect(resolution_combobox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            update_screenshot_info);

    api_combobox->setEnabled(!UISettings::values.has_broken_vulkan && api_combobox->isEnabled());
    ui->api_widget->setEnabled(
        (!UISettings::values.has_broken_vulkan || Settings::IsConfiguringGlobal()) &&
        ui->api_widget->isEnabled());

    if (Settings::IsConfiguringGlobal()) {
        ui->bg_widget->setEnabled(Settings::values.bg_red.UsingGlobal());
    }
}

void ConfigureGraphics::PopulateVSyncModeSelection(bool use_setting) {
    const Settings::RendererBackend backend{GetCurrentGraphicsBackend()};
    if (backend == Settings::RendererBackend::Null) {
        vsync_mode_combobox->setEnabled(false);
        return;
    }
    vsync_mode_combobox->setEnabled(true);

    const int current_index = //< current selected vsync mode from combobox
        vsync_mode_combobox->currentIndex();
    const auto current_mode = //< current selected vsync mode as a VkPresentModeKHR
        current_index == -1 || use_setting
            ? VSyncSettingToMode(Settings::values.vsync_mode.GetValue())
            : vsync_mode_combobox_enum_map[current_index];
    int index{};
    const int device{vulkan_device_combobox->currentIndex()}; //< current selected Vulkan device

    const auto& present_modes = //< relevant vector of present modes for the selected device or API
        backend == Settings::RendererBackend::Vulkan && device > -1 ? device_present_modes[device]
                                                                    : default_present_modes;

    vsync_mode_combobox->clear();
    vsync_mode_combobox_enum_map.clear();
    vsync_mode_combobox_enum_map.reserve(present_modes.size());
    for (const auto present_mode : present_modes) {
        const auto mode_name = TranslateVSyncMode(present_mode, backend);
        if (mode_name.isEmpty()) {
            continue;
        }

        vsync_mode_combobox->insertItem(index, mode_name);
        vsync_mode_combobox_enum_map.push_back(present_mode);
        if (present_mode == current_mode) {
            vsync_mode_combobox->setCurrentIndex(index);
        }
        index++;
    }

    if (!Settings::IsConfiguringGlobal()) {
        vsync_restore_global_button->setVisible(!Settings::values.vsync_mode.UsingGlobal());

        const Settings::VSyncMode global_vsync_mode = Settings::values.vsync_mode.GetValue(true);
        vsync_restore_global_button->setEnabled(
            (IsOpenGL(backend) &&
             (global_vsync_mode == Settings::VSyncMode::Immediate ||
              global_vsync_mode == Settings::VSyncMode::Fifo)) ||
            backend == Settings::RendererBackend::Vulkan);
    }
}

void ConfigureGraphics::UpdateVsyncSetting() const {
    const Settings::RendererBackend backend{GetCurrentGraphicsBackend()};
    if (backend == Settings::RendererBackend::Null || vsync_mode_combobox_enum_map.empty()) {
        return;
    }

    const auto mode = vsync_mode_combobox_enum_map[vsync_mode_combobox->currentIndex()];
    const auto vsync_mode = PresentModeToSetting(mode);
    Settings::values.vsync_mode.SetValue(vsync_mode);
}

void ConfigureGraphics::UpdateDeviceSelection(int device) {
    if (device == -1) {
        return;
    }
    if (GetCurrentGraphicsBackend() == Settings::RendererBackend::Vulkan) {
        vulkan_device = device;
    }
}

ConfigureGraphics::~ConfigureGraphics() = default;

void ConfigureGraphics::SetConfiguration() {}

void ConfigureGraphics::Setup(const ConfigurationShared::Builder& builder) {
    QLayout* api_layout = ui->api_widget->layout();
    QWidget* api_grid_widget = new QWidget(this);
    QVBoxLayout* api_grid_layout = new QVBoxLayout(api_grid_widget);
    api_grid_layout->setContentsMargins(0, 0, 0, 0);
    api_layout->addWidget(api_grid_widget);

    QLayout& graphics_layout = *ui->graphics_widget->layout();

    std::map<u32, QWidget*> hold_graphics;
    std::vector<QWidget*> hold_api;

    for (const auto setting : Settings::values.linkage.by_category[Settings::Category::Renderer]) {
        ConfigurationShared::Widget* widget = [&]() {
            if (setting->Id() == Settings::values.fsr_sharpening_slider.Id()) {
                // FSR needs a reversed slider and a 0.5 multiplier
                return builder.BuildWidget(
                    setting, apply_funcs, ConfigurationShared::RequestType::ReverseSlider, true,
                    0.5f, nullptr, tr("%", "FSR sharpening percentage (e.g. 50%)"));
            } else {
                return builder.BuildWidget(setting, apply_funcs);
            }
        }();

        if (widget == nullptr) {
            continue;
        }
        if (!widget->Valid()) {
            widget->deleteLater();
            continue;
        }

        if (setting->Id() == Settings::values.fsr_sharpening_slider.Id()) {
            // UPGRADE [7]: keep handles so the FSR preset can drive the sharpness slider
            fsr_sharpness_widget = widget;
            fsr_sharpness_slider = widget->slider;
        }

        if (setting->Id() == Settings::values.renderer_backend.Id()) {
            // Add the renderer combobox now so it's at the top
            api_grid_layout->addWidget(widget);
            api_combobox = widget->combobox;
            api_restore_global_button = widget->restore_button;

            if (!Settings::IsConfiguringGlobal()) {
                QObject::connect(api_restore_global_button, &QAbstractButton::clicked,
                                 [this](bool) { UpdateAPILayout(); });

                // Detach API's restore button and place it where we want
                // Lets us put it on the side, and it will automatically scale if there's a
                // second combobox (shader_backend, vulkan_device)
                widget->layout()->removeWidget(api_restore_global_button);
                api_layout->addWidget(api_restore_global_button);
            }
        } else if (setting->Id() == Settings::values.vulkan_device.Id()) {
            // Keep track of vulkan_device's combobox so we can populate it
            hold_api.push_back(widget);
            vulkan_device_combobox = widget->combobox;
            vulkan_device_widget = widget;
        } else if (setting->Id() == Settings::values.vsync_mode.Id()) {
            // Keep track of vsync_mode's combobox so we can populate it
            vsync_mode_combobox = widget->combobox;

            // Since vsync is populated at runtime, we have to manually set up the button for
            // restoring the global setting.
            if (!Settings::IsConfiguringGlobal()) {
                QPushButton* restore_button =
                    ConfigurationShared::Widget::CreateRestoreGlobalButton(
                        Settings::values.vsync_mode.UsingGlobal(), widget);
                restore_button->setEnabled(true);
                widget->layout()->addWidget(restore_button);

                QObject::connect(restore_button, &QAbstractButton::clicked,
                                 [restore_button, this](bool) {
                                     Settings::values.vsync_mode.SetGlobal(true);
                                     PopulateVSyncModeSelection(true);

                                     restore_button->setVisible(false);
                                 });

                std::function<void()> set_non_global = [restore_button, this]() {
                    Settings::values.vsync_mode.SetGlobal(false);
                    UpdateVsyncSetting();
                    restore_button->setVisible(true);
                };
                QObject::connect(widget->combobox, QOverload<int>::of(&QComboBox::activated),
                                 [set_non_global]() { set_non_global(); });
                vsync_restore_global_button = restore_button;
            }
            hold_graphics.emplace(setting->Id(), widget);
        } else if (setting->Id() == Settings::values.aspect_ratio.Id()) {
            // Keep track of the aspect ratio combobox to update other UI tabs that need it
            aspect_ratio_combobox = widget->combobox;
            hold_graphics.emplace(setting->Id(), widget);
        } else if (setting->Id() == Settings::values.resolution_setup.Id()) {
            // Keep track of the resolution combobox to update other UI tabs that need it
            resolution_combobox = widget->combobox;
            hold_graphics.emplace(setting->Id(), widget);
        } else if (setting->Id() == Settings::values.scaling_filter.Id()) {
            // UPGRADE [7]: keep track of the filter combobox to enable/disable the preset
            filter_combobox = widget->combobox;
            hold_graphics.emplace(setting->Id(), widget);
        } else {
            hold_graphics.emplace(setting->Id(), widget);
        }
    }

    for (const auto& [_, widget] : hold_graphics) {
        graphics_layout.addWidget(widget);
    }

    for (auto widget : hold_api) {
        api_grid_layout->addWidget(widget);
    }

    // UPGRADE [7]: FSR preset row. Only in the global configuration dialog to avoid
    // clashing with the per-game widget builder; presets drive the existing resolution
    // combobox and sharpness slider so values apply through the standard apply_funcs.
    if (Settings::IsConfiguringGlobal() && fsr_sharpness_widget && fsr_sharpness_slider &&
        filter_combobox && resolution_combobox) {
        fsr_preset_row = new QWidget(this);
        QHBoxLayout* preset_layout = new QHBoxLayout(fsr_preset_row);
        preset_layout->setContentsMargins(0, 0, 0, 0);

        QLabel* preset_label = new QLabel(tr("FSR Preset:"), fsr_preset_row);
        preset_label->setToolTip(
            tr("Quick presets for FSR upscaling.\nQuality favors internal resolution, "
               "Performance favors sharpening. Custom is selected automatically when the "
               "resolution or sharpness is changed manually.\nFSR gives its best results when "
               "the window has roughly 1x to 4x the pixel area of the internal resolution "
               "(for example 1440p upscaled to a 4K window is 2.25x)."));
        fsr_preset_combobox = new QComboBox(fsr_preset_row);
        fsr_preset_combobox->addItem(tr("Quality"));
        fsr_preset_combobox->addItem(tr("Balanced"));
        fsr_preset_combobox->addItem(tr("Performance"));
        fsr_preset_combobox->addItem(tr("Custom"));
        fsr_preset_combobox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        // UPGRADE [7]: accessibility. Associate the label with the combobox (Alt+mnemonic and
        // screen readers), give both controls explicit accessible names and keep the keyboard
        // focus order sharpness slider -> preset -> next control.
        preset_label->setBuddy(fsr_preset_combobox);
        fsr_preset_combobox->setAccessibleName(tr("FSR preset"));
        fsr_preset_combobox->setAccessibleDescription(preset_label->toolTip());
        fsr_preset_combobox->setFocusPolicy(Qt::StrongFocus);
        fsr_sharpness_slider->setAccessibleName(tr("FSR sharpness"));
        fsr_sharpness_slider->setAccessibleDescription(
            tr("Use the arrow keys to change the sharpening strength, shown in percent."));
        QWidget::setTabOrder(fsr_sharpness_slider, fsr_preset_combobox);

        preset_layout->addWidget(preset_label);
        preset_layout->addWidget(fsr_preset_combobox);
        fsr_preset_row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        const auto apply_preset = [this](int index) {
            Settings::ResolutionSetup target_res{};
            int target_sharpness = 0;
            switch (index) {
            // Note: the slider stores raw RCAS "stops x 100" and is displayed reversed, so a
            // LOWER raw value means MORE sharpening (75 -> 63 %, 50 -> 75 %, 25 -> 88 %).
            // The resulting FSR area ratio depends on the window size; the figures below are
            // for a 4K window (a 1x base of 720p).
            case 0:
                // Quality: 2X internal resolution (1440p -> 4K = 2.25x area, AMD's sweet
                // spot), gentle sharpening
                target_res = Settings::ResolutionSetup::Res2X;
                target_sharpness = 75;
                break;
            case 1:
                // Balanced: 1.5X internal resolution (1080p -> 4K = 4x area, upper end of the
                // recommended 1x-4x range)
                target_res = Settings::ResolutionSetup::Res3_2X;
                target_sharpness = 50;
                break;
            case 2:
                // Performance: 1X internal resolution (720p -> 4K = 9x area, beyond the
                // recommended range, so compensate with the strongest sharpening)
                target_res = Settings::ResolutionSetup::Res1X;
                target_sharpness = 25;
                break;
            default:
                return;
            }
            const int res_index =
                FindIndex(Settings::EnumMetadata<Settings::ResolutionSetup>::Index(),
                          static_cast<int>(target_res));
            if (res_index >= 0) {
                resolution_combobox->setCurrentIndex(res_index);
            }
            fsr_sharpness_slider->setValue(target_sharpness);
        };

        // Manual edits fall back to the Custom entry
        QObject::connect(fsr_sharpness_slider, &QAbstractSlider::valueChanged, this, [this](int) {
            if (!updating_preset) {
                fsr_preset_combobox->setCurrentIndex(3);
            }
        });
        QObject::connect(resolution_combobox, QOverload<int>::of(&QComboBox::activated), this,
                         [this](int) {
                             if (!updating_preset) {
                                 fsr_preset_combobox->setCurrentIndex(3);
                             }
                         });

        QObject::connect(fsr_preset_combobox, QOverload<int>::of(&QComboBox::activated), this,
                         [this, apply_preset](int index) {
                             updating_preset = true;
                             apply_preset(index);
                             updating_preset = false;
                         });

        // The preset only makes sense while FSR is the window adapting filter
        const auto update_preset_enabled = [this]() {
            const int fsr_index =
                FindIndex(Settings::EnumMetadata<Settings::ScalingFilter>::Index(),
                          static_cast<int>(Settings::ScalingFilter::Fsr));
            const bool fsr_active = fsr_index >= 0 && filter_combobox->currentIndex() == fsr_index;
            fsr_preset_row->setEnabled(fsr_active);
        };
        update_preset_enabled();
        QObject::connect(filter_combobox, QOverload<int>::of(&QComboBox::activated), this,
                         update_preset_enabled);

        // Initial preset selection from the current settings
        const auto current_res = Settings::values.resolution_setup.GetValue();
        const int current_sharpness = Settings::values.fsr_sharpening_slider.GetValue();
        int preset_index = 3;
        if (current_res == Settings::ResolutionSetup::Res2X && current_sharpness == 75) {
            preset_index = 0;
        } else if (current_res == Settings::ResolutionSetup::Res3_2X && current_sharpness == 50) {
            preset_index = 1;
        } else if (current_res == Settings::ResolutionSetup::Res1X && current_sharpness == 25) {
            preset_index = 2;
        }
        fsr_preset_combobox->setCurrentIndex(preset_index);

        // Place the row right below the FSR sharpness slider.
        // UPGRADE BUILD FIX: QLayout (the static type of graphics_layout) has no
        // insertWidget member; it is a QBoxLayout API. The graphics widget uses a box
        // layout (see the .ui file), so qobject_cast to QBoxLayout and insert at a
        // position, falling back to plain addWidget for any non-box layout.
        if (auto* box_layout = qobject_cast<QBoxLayout*>(&graphics_layout)) {
            const int sharpness_index = box_layout->indexOf(fsr_sharpness_widget);
            if (sharpness_index >= 0) {
                box_layout->insertWidget(sharpness_index + 1, fsr_preset_row);
            } else {
                box_layout->addWidget(fsr_preset_row);
            }
        } else {
            graphics_layout.addWidget(fsr_preset_row);
        }
    }

    // Background color is too specific to build into the new system, so we manage it here
    // (3 settings, all collected into a single widget with a QColor to manage on top)
    if (Settings::IsConfiguringGlobal()) {
        apply_funcs.push_back([this](bool powered_on) {
            Settings::values.bg_red.SetValue(static_cast<u8>(bg_color.red()));
            Settings::values.bg_green.SetValue(static_cast<u8>(bg_color.green()));
            Settings::values.bg_blue.SetValue(static_cast<u8>(bg_color.blue()));
        });
    } else {
        QPushButton* bg_restore_button = ConfigurationShared::Widget::CreateRestoreGlobalButton(
            Settings::values.bg_red.UsingGlobal(), ui->bg_widget);
        ui->bg_widget->layout()->addWidget(bg_restore_button);

        QObject::connect(bg_restore_button, &QAbstractButton::clicked,
                         [bg_restore_button, this](bool) {
                             const int r = Settings::values.bg_red.GetValue(true);
                             const int g = Settings::values.bg_green.GetValue(true);
                             const int b = Settings::values.bg_blue.GetValue(true);
                             UpdateBackgroundColorButton(QColor::fromRgb(r, g, b));

                             bg_restore_button->setVisible(false);
                             bg_restore_button->setEnabled(false);
                         });

        QObject::connect(ui->bg_button, &QAbstractButton::clicked, [bg_restore_button](bool) {
            bg_restore_button->setVisible(true);
            bg_restore_button->setEnabled(true);
        });

        apply_funcs.push_back([bg_restore_button, this](bool powered_on) {
            const bool using_global = !bg_restore_button->isEnabled();
            Settings::values.bg_red.SetGlobal(using_global);
            Settings::values.bg_green.SetGlobal(using_global);
            Settings::values.bg_blue.SetGlobal(using_global);
            if (!using_global) {
                Settings::values.bg_red.SetValue(static_cast<u8>(bg_color.red()));
                Settings::values.bg_green.SetValue(static_cast<u8>(bg_color.green()));
                Settings::values.bg_blue.SetValue(static_cast<u8>(bg_color.blue()));
            }
        });
    }
}

const QString ConfigureGraphics::TranslateVSyncMode(VkPresentModeKHR mode,
                                                    Settings::RendererBackend backend) const {
    switch (mode) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return IsOpenGL(backend) ? tr("Off")
                                 : QStringLiteral("Immediate (%1)").arg(tr("VSync Off"));
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return QStringLiteral("Mailbox (%1)").arg(tr("Recommended"));
    case VK_PRESENT_MODE_FIFO_KHR:
        return IsOpenGL(backend) ? tr("On")
                                 : QStringLiteral("FIFO (%1)").arg(tr("VSync On"));
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        return QStringLiteral("FIFO Relaxed");
    default:
        return {};
        break;
    }
}

int ConfigureGraphics::FindIndex(u32 enumeration, int value) const {
    for (u32 i = 0; enumeration < combobox_translations.size() &&
                    i < combobox_translations.at(enumeration).size();
         i++) {
        if (combobox_translations.at(enumeration)[i].first == static_cast<u32>(value)) {
            return i;
        }
    }
    return -1;
}

void ConfigureGraphics::ApplyConfiguration() {
    const bool powered_on = system.IsPoweredOn();
    for (const auto& func : apply_funcs) {
        func(powered_on);
    }

    UpdateVsyncSetting();

    Settings::values.vulkan_device.SetGlobal(true);
    const auto api_index = Settings::EnumMetadata<Settings::RendererBackend>::Index();
    if (Settings::IsConfiguringGlobal() ||
        (!Settings::IsConfiguringGlobal() && api_restore_global_button->isEnabled())) {
        auto backend =
            api_index >= combobox_translations.size() ||
                    static_cast<size_t>(api_combobox->currentIndex()) >=
                        combobox_translations.at(api_index).size()
                ? Settings::values.renderer_backend.GetValue()
                : static_cast<Settings::RendererBackend>(
                      combobox_translations.at(api_index)[api_combobox->currentIndex()]
                          .first);
        switch (backend) {
        case Settings::RendererBackend::OpenGL_GLSL:
        case Settings::RendererBackend::OpenGL_GLASM:
        case Settings::RendererBackend::OpenGL_SPIRV:
            // Shader backend is now baked into the RendererBackend value itself.
            break;
        case Settings::RendererBackend::Vulkan:
            Settings::values.vulkan_device.SetGlobal(Settings::IsConfiguringGlobal());
            Settings::values.vulkan_device.SetValue(vulkan_device_combobox->currentIndex());
            break;
        case Settings::RendererBackend::Null:
            break;
        }
    }
}

void ConfigureGraphics::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureGraphics::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigureGraphics::UpdateBackgroundColorButton(QColor color) {
    bg_color = color;

    ui->bg_button->setStyleSheet(
        QStringLiteral("background-color: %1; min-width: 80px;").arg(bg_color.name()));
}

void ConfigureGraphics::UpdateAPILayout() {
    bool runtime_lock = !system.IsPoweredOn();
    bool need_global = !(Settings::IsConfiguringGlobal() || api_restore_global_button->isEnabled());
    vulkan_device = Settings::values.vulkan_device.GetValue(need_global);
    vulkan_device_widget->setEnabled(!need_global && runtime_lock);

    const auto current_backend = GetCurrentGraphicsBackend();
    const bool is_vulkan = current_backend == Settings::RendererBackend::Vulkan;

    vulkan_device_widget->setVisible(is_vulkan);

    if (is_vulkan && static_cast<int>(vulkan_device) < vulkan_device_combobox->count()) {
        vulkan_device_combobox->setCurrentIndex(vulkan_device);
    }
}

void ConfigureGraphics::RetrieveVulkanDevices() {
    vulkan_devices.clear();
    vulkan_devices.reserve(records.size());
    device_present_modes.clear();
    device_present_modes.reserve(records.size());
    for (const auto& record : records) {
        vulkan_devices.push_back(QString::fromStdString(record.name));
        device_present_modes.push_back(record.vsync_support);

        if (record.has_broken_compute) {
            expose_compute_option();
        }
    }
}

Settings::RendererBackend ConfigureGraphics::GetCurrentGraphicsBackend() const {
    const auto selected_backend = [&]() {
        if (!Settings::IsConfiguringGlobal() && !api_restore_global_button->isEnabled()) {
            return Settings::values.renderer_backend.GetValue(true);
        }
        const auto api_index = Settings::EnumMetadata<Settings::RendererBackend>::Index();
        if (api_index >= combobox_translations.size() ||
            static_cast<size_t>(api_combobox->currentIndex()) >=
                combobox_translations.at(api_index).size()) {
            return Settings::values.renderer_backend.GetValue();
        }
        return static_cast<Settings::RendererBackend>(
            combobox_translations.at(api_index)
                .at(api_combobox->currentIndex())
                .first);
    }();

    if (selected_backend == Settings::RendererBackend::Vulkan &&
        UISettings::values.has_broken_vulkan) {
        return Settings::RendererBackend::OpenGL_SPIRV;
    }
    return selected_backend;
}
