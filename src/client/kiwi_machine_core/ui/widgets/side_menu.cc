// Copyright (C) 2023 Yisi Yu
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "ui/widgets/side_menu.h"

#include "models/nes_runtime.h"
#include "ui/main_window.h"
#include "ui/styles.h"
#include "utility/audio_effects.h"
#include "utility/fonts.h"
#include "utility/images.h"
#include "utility/key_mapping_util.h"
#include "utility/localization.h"
#include "utility/math.h"

const int kItemHeight = styles::side_menu::GetItemHeight();
const int kItemMarginBottom = styles::side_menu::GetMarginBottom();
constexpr ImVec2 kItemSpacing(3, 10);
constexpr int kItemAnimationMs = 50;
constexpr int kIconSpacing = 4;
constexpr float kIconSizeScale = .7f;
constexpr ImColor kBackgroundColor = ImColor(21, 149, 5);
const PreferredFontSize kPreferredFontSize(
    styles::side_menu::GetPreferredFontSize());

#define SCALED(x) \
  static_cast<int>((main_window_->window_scale() >= 3.f ? (x) : ((x) / 1.5f)))

SideMenu::SideMenu(MainWindow* main_window, NESRuntimeID runtime_id)
    : Widget(main_window), main_window_(main_window) {
  runtime_data_ = NESRuntime::GetInstance()->GetDataById(runtime_id);
  set_flags(ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
  set_title("SideMenu");
}

SideMenu::~SideMenu() = default;

void SideMenu::Paint() {
  Layout();

  SDL_Rect kBoundsToWindow = MapToWindow(bounds());
  ImGui::GetWindowDrawList()->AddRectFilled(
      ImVec2(kBoundsToWindow.x, kBoundsToWindow.y),
      ImVec2(kBoundsToWindow.x + kBoundsToWindow.w,
             kBoundsToWindow.y + kBoundsToWindow.h),
      kBackgroundColor);

  for (int i = menu_items_.size() - 1; i >= 0; --i) {
    int kIconSize = kIconSizeScale * items_bounds_map_[i].h;
    int kIconLeft = SCALED(kIconSpacing) + items_bounds_map_[i].x;
    int kIconTop =
        items_bounds_map_[i].y + (items_bounds_map_[i].h - kIconSize) / 2;

    std::string menu_content =
        menu_items_[i].string_updater->GetLocalizedString();
    ScopedFont font =
        GetPreferredFont(kPreferredFontSize, menu_content.c_str());
    if (i == current_index_) {
      // Animation
      if (SDL_RectEmpty(&selection_current_rect_in_global_)) {
        selection_current_rect_in_global_ = items_bounds_map_[i];
      }
      selection_target_rect_in_global_ = items_bounds_map_[i];
      float percentage =
          timer_.ElapsedInMilliseconds() / static_cast<float>(kItemAnimationMs);
      if (percentage >= 1.f) {
        percentage = 1.f;
        selection_current_rect_in_global_ = selection_target_rect_in_global_;
      }

      SDL_Rect global_selection_rect =
          MapToWindow(Lerp(selection_current_rect_in_global_,
                           selection_target_rect_in_global_, percentage));
      SDL_Rect global_target_selection_rect =
          MapToWindow(selection_target_rect_in_global_);

      if (activate_) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(global_selection_rect.x, global_selection_rect.y),
            ImVec2(global_selection_rect.x + global_selection_rect.w,
                   global_selection_rect.y + global_selection_rect.h),
            ImColor(255, 255, 255));

        // Calculates text area
        ImVec2 text_size = ImGui::CalcTextSize(menu_content.c_str());
        int text_top = global_target_selection_rect.y +
                       (global_target_selection_rect.h - text_size.y) / 2;
        ImGui::GetWindowDrawList()->AddText(
            font.GetFont(), font.GetFont()->FontSize,
            ImVec2(global_target_selection_rect.x + kIconLeft * 2 + kIconSize,
                   text_top),
            kBackgroundColor, menu_content.c_str());
      } else {
        // A deactivated side menu doesn't paint its text, and has a smaller
        // selection area.
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(global_selection_rect.x, global_selection_rect.y),
            ImVec2(global_selection_rect.w,
                   global_selection_rect.y + global_selection_rect.h),
            ImColor(255, 255, 255));
      }
    } else {
      SDL_Rect global_item_rect = MapToWindow(items_bounds_map_[i]);
      // Calculates text area
      ImVec2 text_size = ImGui::CalcTextSize(menu_content.c_str());
      int text_top =
          global_item_rect.y + (global_item_rect.h - text_size.y) / 2;
      ImGui::GetWindowDrawList()->AddText(
          font.GetFont(), font.GetFont()->FontSize,
          ImVec2(global_item_rect.x + kIconLeft * 2 + kIconSize, text_top),
          ImColor(255, 255, 255), menu_content.c_str());
    }

    // Paint icon
    image_resources::ImageID icon = (i == current_index_)
                                        ? menu_items_[i].highlight_icon
                                        : menu_items_[i].icon;
    SDL_Texture* icon_texture = GetImage(window()->renderer(), icon);
    SDL_Rect icon_rect_to_window =
        MapToWindow(SDL_Rect{kIconLeft, kIconTop, kIconSize, kIconSize});
    ImGui::GetWindowDrawList()->AddImage(
        icon_texture, ImVec2(icon_rect_to_window.x, icon_rect_to_window.y),
        ImVec2(icon_rect_to_window.x + icon_rect_to_window.w,
               icon_rect_to_window.y + icon_rect_to_window.h));
  }
}

void SideMenu::AddMenu(std::unique_ptr<LocalizedStringUpdater> string_updater,
                       image_resources::ImageID icon,
                       image_resources::ImageID highlight_icon,
                       MenuCallbacks callbacks) {
  menu_items_.emplace_back(std::move(string_updater), icon, highlight_icon,
                           callbacks);
  invalidate();

  // When the first widget is added, trigger its selected callback, because it
  // is selected by default.
  if (menu_items_.size() == 1)
    menu_items_[0].callbacks.trigger_callback.Run();
}

int SideMenu::GetSuggestedCollapsedWidth() {
  if (items_bounds_map_.empty())
    return 0;

  return (items_bounds_map_[0].x + SCALED(kIconSpacing)) * 2 +
         items_bounds_map_[0].h * kIconSizeScale;
}

int SideMenu::GetMinExtendedWidth() {
  return 100;
}

bool SideMenu::HandleInputEvent(SDL_KeyboardEvent* k,
                                SDL_ControllerButtonEvent* c) {
  if (!activate_)
    return false;

  // if (!is_finger_down_) { TODO
  if (IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kUp, k) ||
      c && c->button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
    int next_index = (current_index_ <= 0 ? 0 : current_index_ - 1);
    if (next_index != current_index_) {
      PlayEffect(audio_resources::AudioID::kSelect);
      SetIndex(next_index);
    }
    return true;
  }

  if (IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kDown, k) ||
      c && c->button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
    int next_index =
        (current_index_ >= menu_items_.size() - 1 ? current_index_
                                                  : current_index_ + 1);
    if (next_index != current_index_) {
      PlayEffect(audio_resources::AudioID::kSelect);
      SetIndex(next_index);
    }
    return true;
  }

  if (IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kRight, k) ||
      c && c->button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
    if (activate_) {
      PlayEffect(audio_resources::AudioID::kSelect);
      EnterIndex(current_index_);
    }
    return true;
  }

  if (IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kA, k) ||
      IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kStart, k)) {
    if (activate_) {
      PlayEffect(audio_resources::AudioID::kSelect);
      TriggerCurrentItem();
    }
    return true;
  }

  return false;
}

void SideMenu::Layout() {
  if (!bounds_valid_) {
    items_bounds_map_.resize(menu_items_.size());
    SDL_Rect kBoundsToWindow = MapToWindow(bounds());
    const int kX = kBoundsToWindow.x + SCALED(kItemSpacing.x);
    int y = 0;

    for (int i = menu_items_.size() - 1; i >= 0; --i) {
      if (i == menu_items_.size() - 1)
        y = kBoundsToWindow.h - SCALED(kItemMarginBottom) -
            SCALED(kItemHeight) - SCALED(kItemSpacing.y) * 2;

      items_bounds_map_[i] =
          SDL_Rect{kX, y, kBoundsToWindow.w - SCALED(kItemSpacing.x),
                   SCALED(kItemHeight + kItemSpacing.y * 2)};
      y -= SCALED(kItemHeight + kItemSpacing.y * 2);
    }

    bounds_valid_ = true;
  }
}

void SideMenu::SetIndex(int index) {
  current_index_ = index;
  timer_.Reset();
}

void SideMenu::EnterIndex(int index) {
  // If the menu to be entered is not triggered yet, do not enter, and switch
  // back to triggered item.
  if (index != triggered_index_) {
    index = triggered_index_;
    SetIndex(triggered_index_);
  }
  menu_items_[index].callbacks.enter_callback.Run();
}

void SideMenu::TriggerCurrentItem() {
  triggered_index_ = current_index_;
  menu_items_[current_index_].callbacks.trigger_callback.Run();
  menu_items_[current_index_].callbacks.enter_callback.Run();
}

bool SideMenu::FindItemIndexByMousePosition(int x_in_window,
                                            int y_in_window,
                                            int& index_out) {
  // There's no intersection between each item's bounds, so we can find the
  // target item easily
  for (int i = 0; i < items_bounds_map_.size(); ++i) {
    SDL_Rect bounds_to_window = MapToWindow(items_bounds_map_[i]);
    if (Contains(bounds_to_window, x_in_window, y_in_window)) {
      index_out = i;
      return true;
    }
  }
  return false;
}

bool SideMenu::OnKeyPressed(SDL_KeyboardEvent* event) {
  return HandleInputEvent(event, nullptr);
}

bool SideMenu::OnMousePressed(SDL_MouseButtonEvent* event) {
  if (!activate_)
    return true;

  mouse_locked_ = true;
  return true;
}

bool SideMenu::OnMouseReleased(SDL_MouseButtonEvent* event) {
  if (activate_) {
    mouse_locked_ = false;

    if (event->button == SDL_BUTTON_LEFT) {
      int index_before_released = current_index_;
      int index = 0;
      if (FindItemIndexByMousePosition(event->x, event->y, index)) {
        // If the highlight item doesn't change, trigger it.
        if (index_before_released != index) {
          SetIndex(index);
        } else {
          TriggerCurrentItem();
        }
      }
    }
  } else {
    main_window_->ChangeFocus(MainWindow::MainFocus::kSideMenu);
  }
  return true;
}

bool SideMenu::OnControllerButtonPressed(SDL_ControllerButtonEvent* event) {
  return HandleInputEvent(nullptr, event);
}

bool SideMenu::OnControllerAxisMotionEvent(SDL_ControllerAxisEvent* event) {
  return HandleInputEvent(nullptr, nullptr);
}

void SideMenu::OnWindowPreRender() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
}

void SideMenu::OnWindowPostRender() {
  ImGui::PopStyleVar(2);
}

SideMenu::MenuItem::MenuItem(
    std::unique_ptr<LocalizedStringUpdater> string_updater,
    image_resources::ImageID icon,
    image_resources::ImageID highlight_icon,
    MenuCallbacks callbacks) {
  this->string_updater = std::move(string_updater);
  this->icon = icon;
  this->highlight_icon = highlight_icon;
  this->callbacks = callbacks;
}