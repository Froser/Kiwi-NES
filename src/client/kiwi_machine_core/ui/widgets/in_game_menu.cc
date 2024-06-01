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

#include "ui/widgets/in_game_menu.h"

#include <imgui.h>
#include <climits>

#include "build/kiwi_defines.h"
#include "ui/application.h"
#include "ui/main_window.h"
#include "ui/styles.h"
#include "ui/widgets/canvas.h"
#include "utility/audio_effects.h"
#include "utility/key_mapping_util.h"
#include "utility/localization.h"
#include "utility/math.h"

constexpr int kMoveSpeed = 200;

namespace {

InGameMenu::SettingsItemContext MakePromptSettingsItemContext(bool go_left) {
  InGameMenu::SettingsItemContext context;
  context.type = InGameMenu::SettingsItemContext::SettingsItemType::kPrompt;
  context.go_left = go_left;
  return context;
}

InGameMenu::SettingsItemContext MakePromptSettingsItemComplex(
    const SDL_Rect& complex_item_rect) {
  InGameMenu::SettingsItemContext context;
  context.type = InGameMenu::SettingsItemContext::SettingsItemType::kComplex;
  context.bounds.items_bounds = complex_item_rect;
  return context;
}

void DrawTrianglePrompt(bool has_left,
                        bool has_right,
                        const Triangle& left,
                        const Triangle& right) {
  if (!has_left) {
    ImGui::GetWindowDrawList()->AddTriangle(left.point[0], left.point[1],
                                            left.point[2], IM_COL32_WHITE);
  } else {
    ImGui::GetWindowDrawList()->AddTriangleFilled(
        left.point[0], left.point[1], left.point[2], IM_COL32_WHITE);
  }

  if (!has_right) {
    ImGui::GetWindowDrawList()->AddTriangle(right.point[0], right.point[1],
                                            right.point[2], IM_COL32_WHITE);
  } else {
    ImGui::GetWindowDrawList()->AddTriangleFilled(
        right.point[0], right.point[1], right.point[2], IM_COL32_WHITE);
  }
}

}  // namespace

InGameMenu::InGameMenu(MainWindow* main_window,
                       NESRuntimeID runtime_id,
                       MenuItemCallback menu_callback,
                       SettingsItemCallback settings_callback)
    : Widget(main_window),
      main_window_(main_window),
      menu_callback_(menu_callback),
      settings_callback_(settings_callback) {
  set_flags(ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
  set_title("InGameMenu");
  runtime_data_ = NESRuntime::GetInstance()->GetDataById(runtime_id);
  SDL_assert(runtime_data_);

  loading_widget_ = std::make_unique<LoadingWidget>(main_window);
}

InGameMenu::~InGameMenu() {
  if (snapshot_)
    SDL_DestroyTexture(snapshot_);
}

void InGameMenu::SetFirstSelection() {
  // Set first available index as default.
  int selection = -1;
  do {
    ++selection;
    if (selection < 0)
      selection = static_cast<int>(MenuItem::kMax) - 1;
    else
      selection %= static_cast<int>(MenuItem::kMax);
  } while (hide_menus_.find(selection) != hide_menus_.end());
  current_selection_ = static_cast<MenuItem>(selection);
}

void InGameMenu::Paint() {
  if (first_paint_) {
    SetFirstSelection();
    RequestCurrentThumbnail();

    first_paint_ = false;
  }

  CleanUpSettingsItemRects();

  LayoutImmediateContext context = PreLayoutImmediate();
  DrawBackgroundImmediate(context);
  DrawMenuItemsImmediate(context);
  DrawSelectionImmediate(context);
}

bool InGameMenu::OnKeyPressed(SDL_KeyboardEvent* event) {
  return HandleInputEvent(event, nullptr);
}

bool InGameMenu::OnControllerButtonPressed(SDL_ControllerButtonEvent* event) {
  return HandleInputEvent(nullptr, event);
}

bool InGameMenu::OnControllerAxisMotionEvent(SDL_ControllerAxisEvent* event) {
  return HandleInputEvent(nullptr, nullptr);
}

bool InGameMenu::OnMousePressed(SDL_MouseButtonEvent* event) {
  return HandleMouseOrFingerEvents(MouseOrFingerEventType::kMousePressed,
                                   event->x, event->y);
}

bool InGameMenu::HandleInputEvent(SDL_KeyboardEvent* k,
                                  SDL_ControllerButtonEvent* c) {
  if (IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kUp, k) ||
      c && c->button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
    PlayEffect(audio_resources::AudioID::kSelect);
    MoveSelection(true);
    return true;
  }

  if (IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kDown, k) ||
      c && c->button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
    PlayEffect(audio_resources::AudioID::kSelect);
    MoveSelection(false);
    return true;
  }

  if (IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kA, k) ||
      c && c->button == SDL_CONTROLLER_BUTTON_A) {
    HandleMenuItemForCurrentSelection();
    return true;
  }

  if (IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kB, k) ||
      c && c->button == SDL_CONTROLLER_BUTTON_B) {
    PlayEffect(audio_resources::AudioID::kBack);
    if (settings_entered_)
      settings_entered_ = false;
    else
      menu_callback_.Run(MenuItem::kContinue, 0);
    return true;
  }

  if (IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kLeft, k) ||
      c && c->button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
    HandleSettingsItemForCurrentSelection(MakePromptSettingsItemContext(true));
    return true;
  }

  if (IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kRight, k) ||
      c && c->button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
    HandleSettingsItemForCurrentSelection(MakePromptSettingsItemContext(false));
    return true;
  }

  return false;
}

void InGameMenu::HandleMenuItemForCurrentSelection() {
  if (current_selection_ == MenuItem::kOptions) {
    PlayEffect(audio_resources::AudioID::kSelect);
    settings_entered_ = true;
  } else {
    if (current_selection_ == MenuItem::kLoadState ||
        current_selection_ == MenuItem::kSaveState) {
      // Saving or loading states will pass a parameter to show which state
      // to be saved.
      PlayEffect(audio_resources::AudioID::kStart);
      menu_callback_.Run(current_selection_, which_state_);
    } else if (current_selection_ == MenuItem::kLoadAutoSave) {
      PlayEffect(audio_resources::AudioID::kStart);
      menu_callback_.Run(current_selection_, state_timestamp_);
    } else {
      if (current_selection_ == MenuItem::kToGameSelection)
        PlayEffect(audio_resources::AudioID::kBack);
      else
        PlayEffect(audio_resources::AudioID::kStart);

      menu_callback_.Run(current_selection_, 0);
    }
  }
}

void InGameMenu::HandleSettingsItemForCurrentSelection(
    SettingsItemContext context) {
  if (settings_entered_) {
    settings_callback_.Run(current_setting_, context);
  } else {
    HandleSettingsItemForCurrentSelectionInternal(context);
  }
}

void InGameMenu::HandleSettingsItemForCurrentSelectionInternal(
    SettingsItemContext context) {
  if (context.type == SettingsItemContext::SettingsItemType::kPrompt) {
    if (context.go_left) {
      if (current_selection_ == MenuItem::kSaveState ||
          current_selection_ == MenuItem::kLoadState) {
        --which_state_;
        if (which_state_ < 0)
          which_state_ = NESRuntime::Data::MaxSaveStates - 1;
        RequestCurrentThumbnail();
      } else if (current_selection_ == MenuItem::kLoadAutoSave) {
        const kiwi::nes::RomData* rom_data =
            runtime_data_->emulator->GetRomData();
        SDL_assert(rom_data);
        RequestCurrentSaveStatesCount();
        if (which_autosave_state_slot_ < current_auto_states_count_) {
          ++which_autosave_state_slot_;
          RequestCurrentThumbnail();
        }
      }
    } else {
      if (current_selection_ == MenuItem::kSaveState ||
          current_selection_ == MenuItem::kLoadState) {
        which_state_ = (which_state_ + 1) % NESRuntime::Data::MaxSaveStates;
        RequestCurrentThumbnail();
      } else if (current_selection_ == MenuItem::kLoadAutoSave) {
        if (which_autosave_state_slot_ > 0) {
          --which_autosave_state_slot_;
          RequestCurrentThumbnail();
        }
      }
    }
  }
}

void InGameMenu::MoveSelection(bool up) {
  if (!settings_entered_) {
    int selection = static_cast<int>(current_selection_);
    MenuItem last_selection = static_cast<MenuItem>(selection);
    do {
      selection = up ? selection - 1 : selection + 1;
      if (selection < 0)
        selection = static_cast<int>(MenuItem::kMax) - 1;
      else
        selection %= static_cast<int>(MenuItem::kMax);
    } while (hide_menus_.find(selection) != hide_menus_.end());
    current_selection_ = static_cast<MenuItem>(selection);

    if (current_selection_ == MenuItem::kLoadAutoSave) {
      which_autosave_state_slot_ = 0;
      state_timestamp_ = 0;
      RequestCurrentSaveStatesCount();
      RequestCurrentThumbnail();
    } else if ((current_selection_ == MenuItem::kSaveState ||
                current_selection_ == MenuItem::kLoadState) &&
               (last_selection != MenuItem::kSaveState &&
                last_selection != MenuItem::kLoadState)) {
      // When enter load/save state, request thumbnail.
      RequestCurrentThumbnail();
    }
  } else {
    int selection = static_cast<int>(current_setting_);
    selection = up ? selection - 1 : selection + 1;
    if (selection < 0)
      selection = static_cast<int>(SettingsItem::kMax) - 1;
    else
      selection %= static_cast<int>(SettingsItem::kMax);
    current_setting_ = static_cast<SettingsItem>(selection);
  }
}

void InGameMenu::Close() {
  set_visible(false);
}

void InGameMenu::Show() {
  SetFirstSelection();
  set_visible(true);
}

void InGameMenu::RequestCurrentThumbnail() {
  currently_has_snapshot_ = false;
  is_loading_snapshot_ = true;
  const kiwi::nes::RomData* rom_data = runtime_data_->emulator->GetRomData();
  // Settings menu also use this class, but no ROM is loaded.
  if (rom_data) {
    if (current_selection_ == MenuItem::kLoadAutoSave) {
      runtime_data_->GetAutoSavedState(
          rom_data->crc, which_autosave_state_slot_,
          kiwi::base::BindOnce(&InGameMenu::OnGotState,
                               kiwi::base::Unretained(this)));
    } else {
      runtime_data_->GetState(
          rom_data->crc, which_state_,
          kiwi::base::BindOnce(&InGameMenu::OnGotState,
                               kiwi::base::Unretained(this)));
    }
  }
}

void InGameMenu::RequestCurrentSaveStatesCount() {
  const kiwi::nes::RomData* rom_data = runtime_data_->emulator->GetRomData();
  if (rom_data) {
    runtime_data_->GetAutoSavedStatesCount(
        rom_data->crc, kiwi::base::BindOnce(
                           [](InGameMenu* this_menu, int count) {
                             this_menu->current_auto_states_count_ = count;
                           },
                           this));
  }
}

void InGameMenu::OnGotState(const NESRuntime::Data::StateResult& state_result) {
  is_loading_snapshot_ = false;
  if (state_result.success && !state_result.state_data.empty()) {
    currently_has_snapshot_ = true;
    SDL_assert(!state_result.thumbnail_data.empty());
    if (!snapshot_) {
      snapshot_ = SDL_CreateTexture(
          window()->renderer(), SDL_PIXELFORMAT_ARGB8888,
          SDL_TEXTUREACCESS_STREAMING, Canvas::kNESFrameDefaultWidth,
          Canvas::kNESFrameDefaultHeight);
    }

    constexpr int kColorComponents = 4;
    SDL_UpdateTexture(snapshot_, nullptr, state_result.thumbnail_data.data(),
                      Canvas::kNESFrameDefaultWidth * kColorComponents *
                          sizeof(state_result.thumbnail_data.data()[0]));

    // state_timestamp_ will only be used in showing auto-saved state's title.
    state_timestamp_ = state_result.slot_or_timestamp;
  } else {
    currently_has_snapshot_ = false;
  }
}

void InGameMenu::HideMenu(int index) {
  hide_menus_.insert(index);
}

InGameMenu::LayoutImmediateContext InGameMenu::PreLayoutImmediate() {
  LayoutImmediateContext context;
  SDL_Rect safe_area_insets = main_window_->GetSafeAreaInsets();
  context.window_pos = ImGui::GetWindowPos();
  context.window_size = ImGui::GetWindowSize();

  context.font_size = main_window_->window_scale() > 2.f
                          ? PreferredFontSize::k2x
                          : PreferredFontSize::k1x;

  // Calculates the safe area on iPhone or other devices.
  context.window_pos.x += safe_area_insets.x;
  context.window_pos.y += safe_area_insets.y;
  context.window_size.x -= safe_area_insets.x + safe_area_insets.w;
  context.window_size.y -= safe_area_insets.y + safe_area_insets.h;

  // Menu items
  context.menu_items = {
      GetLocalizedString(string_resources::IDR_IN_GAME_MENU_CONTINUE).c_str(),
      GetLocalizedString(string_resources::IDR_IN_GAME_MENU_LOAD_AUTO_SAVE)
          .c_str(),
      GetLocalizedString(string_resources::IDR_IN_GAME_MENU_LOAD_STATE).c_str(),
      GetLocalizedString(string_resources::IDR_IN_GAME_MENU_SAVE_STATE).c_str(),
      GetLocalizedString(string_resources::IDR_IN_GAME_MENU_OPTIONS).c_str(),
      GetLocalizedString(string_resources::IDR_IN_GAME_MENU_RESET_GAME).c_str(),
      GetLocalizedString(string_resources::IDR_IN_GAME_MENU_BACK_TO_MAIN)
          .c_str(),
  };

  // Options
  context.options_items = {
      GetLocalizedString(string_resources::IDR_IN_GAME_MENU_VOLUME).c_str(),
      GetLocalizedString(string_resources::IDR_IN_GAME_MENU_WINDOW_SIZE)
          .c_str(),
      GetLocalizedString(string_resources::IDR_IN_GAME_MENU_JOYSTICKS).c_str(),
      GetLocalizedString(string_resources::IDR_IN_GAME_MENU_LANGUAGE).c_str(),
  };

  // Handlers' order must match drawing order
  context.options_handlers = {
      &InGameMenu::DrawMenuItemsImmediate_DrawMenuItems_Options_Volume,
      &InGameMenu::DrawMenuItemsImmediate_DrawMenuItems_Options_WindowSize,
      &InGameMenu::DrawMenuItemsImmediate_DrawMenuItems_Options_Joysticks,
      &InGameMenu::DrawMenuItemsImmediate_DrawMenuItems_Options_Languages,
  };

  // On PC apps, it may has a debug menu.
  const_cast<int&>(context.title_menu_height) = context.window_pos.y;

  const_cast<int&>(context.window_center_x) =
      context.window_pos.x + context.window_size.x / 2;

  return context;
}

void InGameMenu::DrawBackgroundImmediate(LayoutImmediateContext& context) {
  ImDrawList* bg_draw_list = ImGui::GetBackgroundDrawList();
  bg_draw_list->AddRectFilled(
      context.window_pos,
      ImVec2(context.window_pos.x + context.window_size.x + 1,
             context.window_pos.y + context.window_size.y + 1),
      IM_COL32(0, 0, 0, 196));

  // Draw a new vertical line in the middle.
  ImGui::GetWindowDrawList()->AddLine(
      ImVec2(context.window_center_x, 0),
      ImVec2(context.window_center_x,
             context.window_pos.y + context.window_size.y),
      IM_COL32_WHITE);
}

void InGameMenu::DrawMenuItemsImmediate(LayoutImmediateContext& context) {
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                      ImVec2(0, styles::in_game_menu::GetOptionsSpacing()));
  DrawMenuItemsImmediate_LayoutMenuItems(context);
  DrawMenuItemsImmediate_DrawMenuItems(context);

  ImGui::PopStyleVar();
}

void InGameMenu::DrawMenuItemsImmediate_LayoutMenuItems(
    LayoutImmediateContext& context) {
  ScopedFont font = GetPreferredFont(context.font_size, context.menu_items[0]);
  context.menu_font_size = font.GetFont()->FontSize;

  int min_menu_x = INT_MAX;
  const int kMenuY = ImGui::GetCursorPosY();
  int current_selection = 0;
  for (const char* item : context.menu_items) {
    if (hide_menus_.find(current_selection++) != hide_menus_.end())
      continue;

    ImVec2 text_size = ImGui::CalcTextSize(item);
    int x = context.window_center_x - kMenuItemMargin - text_size.x;
    if (min_menu_x < x)
      min_menu_x = x;
    ImGui::Dummy(text_size);
  }

  ImVec2 current_cursor = ImGui::GetCursorPos();
  ImVec2 menu_size =
      ImVec2(context.window_center_x - kMenuItemMargin - min_menu_x,
             current_cursor.y - kMenuY);
  ImGui::SetCursorPosY(context.window_pos.y +
                       (context.window_size.y - menu_size.y) / 2);
}

void InGameMenu::DrawMenuItemsImmediate_DrawMenuItems(
    LayoutImmediateContext& context) {
  ScopedFont font = GetPreferredFont(context.font_size, context.menu_items[0]);
  int current_selection = 0;
  for (const char* item : context.menu_items) {
    if (hide_menus_.find(current_selection) != hide_menus_.end()) {
      ++current_selection;
      continue;
    }

    context.menu_tops[current_selection] = ImGui::GetCursorPosY();
    ImVec2 text_size = ImGui::CalcTextSize(item);
    ImGui::SetCursorPosX(context.window_center_x - kMenuItemMargin -
                         text_size.x);
    if (current_selection == static_cast<int>(current_selection_))
      ImGui::TextColored(ImVec4(0.f, 0.f, 0.f, 1.f), "%s", item);
    else
      ImGui::Text("%s", item);

    ++current_selection;
  }

  DrawMenuItemsImmediate_DrawMenuItems_SaveLoad(context);
  DrawMenuItemsImmediate_DrawMenuItems_Options(context);
}

void InGameMenu::DrawMenuItemsImmediate_DrawMenuItems_SaveLoad(
    LayoutImmediateContext& context) {
  // Draw save & load thumbnail
  const int kThumbnailWidth = styles::in_game_menu::GetSnapshotThumbnailWidth(
      main_window_->IsLandscape(), main_window_->window_scale());
  const int kThumbnailHeight = styles::in_game_menu::GetSnapshotThumbnailHeight(
      main_window_->IsLandscape(), main_window_->window_scale());

  if (current_selection_ == MenuItem::kSaveState ||
      current_selection_ == MenuItem::kLoadAutoSave ||
      current_selection_ == MenuItem::kLoadState) {
    SDL_Rect right_side_rect{context.window_center_x,
                             static_cast<int>(context.window_pos.y),
                             static_cast<int>(context.window_size.x / 2 + 1),
                             static_cast<int>(context.window_size.y + 1)};
    ImVec2 thumbnail_pos(
        right_side_rect.x + (right_side_rect.w - kThumbnailWidth) / 2,
        right_side_rect.y + (right_side_rect.h - kThumbnailHeight) / 2);
    ImGui::SetCursorPos(thumbnail_pos);
    ImVec2 p0(thumbnail_pos.x, thumbnail_pos.y + context.title_menu_height);
    ImVec2 p1(thumbnail_pos.x + kThumbnailWidth,
              thumbnail_pos.y + kThumbnailHeight + context.title_menu_height);
    ImGui::GetWindowDrawList()->AddRect(ImVec2(p0.x, p0.y), ImVec2(p1.x, p1.y),
                                        IM_COL32_WHITE);

    // Draw triangle, to switch states.
    // Center of the snapshot rect
    const int kSnapshotPromptHeight =
        styles::in_game_menu::GetSnapshotPromptHeight(
            main_window_->window_scale());
    const int kSnapshotPromptWidth = kSnapshotPromptHeight * .8f;

    constexpr int kSnapshotPromptSpacing = 10;
    const int kSnapshotPromptY =
        p0.y + (p1.y - p0.y - kSnapshotPromptHeight) / 2;

    bool left_enabled = true;
    bool right_enabled = true;

    if (current_selection_ == MenuItem::kLoadAutoSave) {
      if (which_autosave_state_slot_ == 0) {
        right_enabled = false;
      }

      const kiwi::nes::RomData* rom_data =
          runtime_data_->emulator->GetRomData();
      SDL_assert(rom_data);
      if (which_autosave_state_slot_ == current_auto_states_count_)
        left_enabled = false;
    }

    Triangle prompt_left{
        ImVec2(p0.x - kSnapshotPromptSpacing - kSnapshotPromptWidth,
               kSnapshotPromptY + kSnapshotPromptHeight / 2),
        ImVec2(p0.x - kSnapshotPromptSpacing, kSnapshotPromptY),
        ImVec2(p0.x - kSnapshotPromptSpacing,
               kSnapshotPromptY + kSnapshotPromptHeight)};
    Triangle prompt_right{
        ImVec2(p1.x + kSnapshotPromptSpacing, kSnapshotPromptY),
        ImVec2(p1.x + kSnapshotPromptSpacing,
               kSnapshotPromptY + kSnapshotPromptHeight),
        ImVec2(p1.x + kSnapshotPromptSpacing + kSnapshotPromptWidth,
               kSnapshotPromptY + kSnapshotPromptHeight / 2)};
    DrawTrianglePrompt(left_enabled, right_enabled, prompt_left, prompt_right);
    AddRectForSettingsItemPrompt(current_setting_, prompt_left.BoundingBox(),
                                 prompt_right.BoundingBox());

    // When the state is saved, RequestCurrentThumbnail() should be invoked. It
    // will create a snapshot texture.
    if (is_loading_snapshot_) {
      SDL_Rect spin_aabb = loading_widget_->CalculateCircleAABB(nullptr);
      ImVec2 spin_size(spin_aabb.w, spin_aabb.h);
      SDL_Rect loading_bounds{
          static_cast<int>(p0.x + (p1.x - p0.x - spin_size.x) / 2 +
                           context.title_menu_height),
          static_cast<int>(p0.y + (p1.y - p0.y - spin_size.y) / 2 +
                           context.title_menu_height),
          20, 20};
      loading_widget_->set_spinning_bounds(loading_bounds);
      loading_widget_->Paint();
    } else if (currently_has_snapshot_) {
      SDL_assert(snapshot_);
      ImGui::Image(snapshot_, ImVec2(kThumbnailWidth, kThumbnailHeight));
    } else {
#if KIWI_IOS
      std::unique_ptr<ScopedFont> no_state_font;
      if (!main_window_->IsLandscape()) {
        no_state_font = std::make_unique<ScopedFont>(FontType::kSystemDefault);
      }
#endif
      const char* kNoStateStr =
          GetLocalizedString(string_resources::IDR_IN_GAME_MENU_NO_STATE)
              .c_str();
      ImVec2 text_size = ImGui::CalcTextSize(kNoStateStr);
      ImGui::SetCursorPos(ImVec2(p0.x + (p1.x - p0.x - text_size.x) / 2,
                                 p0.y + (p1.y - p0.y - text_size.y) / 2));
      ImGui::Text("%s", kNoStateStr);
    }

    // Draw text
    {
      std::string state_slot_str;
      if (current_selection_ == MenuItem::kLoadAutoSave) {
        if (state_timestamp_) {
          // Show the title as auto save's date.
          // Format timestamp.
          time_t time(state_timestamp_);
          state_slot_str = asctime(localtime(&time));
        }
      } else {
        // Slot number in string starts at 1.
        state_slot_str =
            GetLocalizedString(string_resources::IDR_IN_GAME_MENU_SLOT) +
            kiwi::base::NumberToString(which_state_ + 1);
      }

      {
        ScopedFont slot_font(styles::in_game_menu::GetSlotNameFontType(
            main_window_->IsLandscape(), state_slot_str.c_str()));
        ImVec2 text_size = ImGui::CalcTextSize(state_slot_str.c_str());
        ImGui::SetCursorPos(ImVec2(p0.x + (p1.x - p0.x - text_size.x) / 2,
                                   p1.y + kSnapshotPromptSpacing));
        ImGui::Text("%s", state_slot_str.c_str());
      }
    }
  }
}

void InGameMenu::DrawMenuItemsImmediate_DrawMenuItems_Options(
    LayoutImmediateContext& context) {
  // Draw settings
  if (current_selection_ == MenuItem::kOptions) {
    DrawMenuItemsImmediate_DrawMenuItems_OptionsLayout(context);
    DrawMenuItemsImmediate_DrawMenuItems_Options_PaintEachOption(context);
  }
}

void InGameMenu::DrawMenuItemsImmediate_DrawMenuItems_OptionsLayout(
    LayoutImmediateContext& context) {
  ScopedFont option_font = GetPreferredFont(
      context.font_size, context.options_items[0], FontType::kSystemDefault);

  int window_scaling_for_options =
      static_cast<int>(main_window_->window_scale());
  if (main_window_->is_fullscreen())
    window_scaling_for_options = kMaxScaling;
  if (window_scaling_for_options < 2)
    window_scaling_for_options = 2;
  else if (window_scaling_for_options > 4)
    window_scaling_for_options = 4;

  const int kSettingsY = ImGui::GetCursorPosY();
  for (const char* item : context.options_items) {
    ImVec2 text_size = ImGui::CalcTextSize(item);
    ImGui::Dummy(text_size);
  }

  // Beginning of calculating settings' position
  // Including main settings, volume bar, window sizes, joysticks, etc.
  // Volume bar
  context.volume_bar_height = 7 * window_scaling_for_options;
  context.options_items_spacing = 3 * window_scaling_for_options;
  context.window_scaling_for_options = window_scaling_for_options;
  ImGui::Dummy(ImVec2(1, context.volume_bar_height));

  // Window size
  {
    ScopedFont scoped_font(
        GetPreferredFontType(context.font_size, context.options_items[1]));
    ImGui::Dummy(ImVec2(1, ImGui::GetFontSize()));
  }
  // Joysticks
  {
    ScopedFont scoped_font(
        GetPreferredFontType(context.font_size, context.options_items[2]));
    ImGui::Dummy(ImVec2(1, ImGui::GetFontSize()));
  }
  // Languages
  {
    ScopedFont scoped_font(
        GetPreferredFontType(context.font_size, context.options_items[3]));
    ImGui::Dummy(ImVec2(1, ImGui::GetFontSize()));
  }

  ImVec2 current_cursor = ImGui::GetCursorPos();
  ImGui::SetCursorPosY((context.window_pos.y + context.window_size.y -
                        (current_cursor.y - kSettingsY)) /
                       2);
  // End of calculating settings' position
}

void InGameMenu::DrawMenuItemsImmediate_DrawMenuItems_Options_PaintEachOption(
    LayoutImmediateContext& context) {
  for (int i = 0; i < context.options_items.size(); ++i) {
    const char* item = context.options_items[i];
    ImVec2 text_size = ImGui::CalcTextSize(item);
    ImGui::SetCursorPosX(context.window_center_x + kMenuItemMargin);
    ImGui::Text("%s", item);
    std::invoke(context.options_handlers[i], this, context);
  }
}

void InGameMenu::DrawMenuItemsImmediate_DrawMenuItems_Options_Volume(
    LayoutImmediateContext& context) {
#if !KIWI_MOBILE
  // PC application has a volume bar
  float prompt_height = context.volume_bar_height;
  float prompt_width = prompt_height * .8f;
  ImVec2 p0(context.window_center_x + kMenuItemMargin + prompt_width +
                kMenuItemMargin,
            ImGui::GetCursorPosY() + context.title_menu_height);
  ImVec2 p1(context.window_size.x - kMenuItemMargin,
            ImGui::GetCursorPosY() + context.volume_bar_height +
                context.title_menu_height);

  ImGui::GetWindowDrawList()->AddRect(p0, p1, IM_COL32_WHITE);
  const SDL_Rect kVolumeMenuBounds{
      static_cast<int>(p0.x),
      static_cast<int>(p0.y) -
          context.title_menu_height,  // Rect draw is relative to window's
                                      // position, so minus menu height
                                      // here, to get the position
                                      // relative to this widget
      static_cast<int>(p1.x - p0.x), context.volume_bar_height};
  AddRectForSettingsItem(SettingsItem::kVolume, kVolumeMenuBounds);

  const SDL_Rect kVolumeOperationBounds = {
      static_cast<int>(p0.x), static_cast<int>(p0.y),
      static_cast<int>(p1.x - p0.x), context.volume_bar_height};
  AddRectForSettingsItemPrompt(SettingsItem::kVolume, kVolumeOperationBounds);
  ImGui::Dummy(ImVec2(p1.x - p0.x, p1.y - p0.y));

  float volume = runtime_data_->emulator->GetVolume();
  const int kInnerBarWidth = (p1.x - p0.x) - 2;
  ImVec2 inner_p0(p0.x + 1, p0.y + 1);
  ImVec2 inner_p1(p0.x + 1 + (kInnerBarWidth * volume), p1.y - 1);
  ImGui::GetWindowDrawList()->AddRectFilled(inner_p0, inner_p1, IM_COL32_WHITE);

  if (settings_entered_ && current_setting_ == SettingsItem::kVolume) {
    ImGui::GetWindowDrawList()->AddTriangleFilled(
        ImVec2(p0.x - prompt_width - context.options_items_spacing, p0.y),
        ImVec2(p0.x - prompt_width - context.options_items_spacing,
               p0.y + prompt_height),
        ImVec2(p0.x - context.options_items_spacing, p0.y + prompt_height / 2),
        IM_COL32_WHITE);
  }
#else
  // There are only 2 options on mobiles: On or Off.emulator->GetVolume();
  ScopedFont scoped_font(GetPreferredFontType(context.font_size, kVolumeStr));
  float prompt_height = ImGui::GetFontSize();
  float prompt_width = prompt_height * .8f;

  ImVec2 window_text_size = ImGui::CalcTextSize(kVolumeStr);
  ImGui::SetCursorPosX(context.window_center_x + context.window_center_x +
                       (context.window_size.x / 2 - window_text_size.x) / 2);
  int text_y = ImGui::GetCursorPosY();
  ImGui::Text("%s", kVolumeStr);

  settings_positions_[SettingsItem::kVolume] =
      SDL_Rect{context.window_center_x, text_y, context.window_center_x,
               static_cast<int>(ImGui::GetCursorPosY() - text_y)};

  if (settings_entered_ && current_setting_ == SettingsItem::kVolume) {
    bool has_no_left = volume <= 0.f;
    bool has_right = has_no_left;

    ImVec2 triangle_p0(context.window_center_x + context.window_center_x +
                           prompt_width + context.window_center_x,
                       text_y + kTitleMenuHeight);

    AddRectForSettingsItem(
        0,
        SDL_Rect{static_cast<int>(triangle_p0.x - prompt_width -
                                  context.volume_bar_spacing),
                 static_cast<int>(triangle_p0.y),
                 static_cast<int>(prompt_width),
                 static_cast<int>(prompt_height)},
        SDL_Rect{static_cast<int>(context.window_pos.x + context.window_size.x -
                                  context.window_center_x - prompt_width),
                 static_cast<int>(triangle_p0.y),
                 static_cast<int>(prompt_width),
                 static_cast<int>(prompt_height)});

    if (has_no_left) {
      ImGui::GetWindowDrawList()->AddTriangle(
          ImVec2(triangle_p0.x - prompt_width - context.volume_bar_spacing,
                 triangle_p0.y + prompt_height / 2),
          ImVec2(triangle_p0.x - context.volume_bar_spacing, triangle_p0.y),
          ImVec2(triangle_p0.x - context.volume_bar_spacing,
                 triangle_p0.y + prompt_height),
          IM_COL32_WHITE);
    } else {
      ImGui::GetWindowDrawList()->AddTriangleFilled(
          ImVec2(triangle_p0.x - prompt_width - context.volume_bar_spacing,
                 triangle_p0.y + prompt_height / 2),
          ImVec2(triangle_p0.x - context.volume_bar_spacing, triangle_p0.y),
          ImVec2(triangle_p0.x - context.volume_bar_spacing,
                 triangle_p0.y + prompt_height),
          IM_COL32_WHITE);
    }

    if (has_right) {
      ImGui::GetWindowDrawList()->AddTriangleFilled(
          ImVec2(context.window_pos.x + context.window_size.x -
                     context.window_center_x - prompt_width,
                 triangle_p0.y),
          ImVec2(context.window_pos.x + context.window_size.x -
                     context.window_center_x - prompt_width,
                 triangle_p0.y + prompt_height),
          ImVec2(context.window_pos.x + context.window_size.x -
                     context.window_center_x,
                 triangle_p0.y + prompt_height / 2),
          IM_COL32_WHITE);
    } else {
      ImGui::GetWindowDrawList()->AddTriangle(
          ImVec2(context.window_pos.x + context.window_size.x -
                     context.window_center_x - prompt_width,
                 triangle_p0.y),
          ImVec2(context.window_pos.x + context.window_size.x -
                     context.window_center_x - prompt_width,
                 triangle_p0.y + prompt_height),
          ImVec2(context.window_pos.x + context.window_size.x -
                     context.window_center_x,
                 triangle_p0.y + prompt_height / 2),
          IM_COL32_WHITE);
    }
  }
#endif
}

void InGameMenu::DrawMenuItemsImmediate_DrawMenuItems_Options_WindowSize(
    LayoutImmediateContext& context) {
#if !KIWI_MOBILE
  const char* kWindowSizes[] = {
      GetLocalizedString(string_resources::IDR_IN_GAME_MENU_SMALL).c_str(),
      GetLocalizedString(string_resources::IDR_IN_GAME_MENU_NORMAL).c_str(),
      GetLocalizedString(string_resources::IDR_IN_GAME_MENU_LARGE).c_str(),
      GetLocalizedString(string_resources::IDR_IN_GAME_MENU_FULLSCREEN)
          .c_str()};
  const char* kSizeStr =
      main_window_->is_fullscreen()
          ? kWindowSizes[3]
          : kWindowSizes[context.window_scaling_for_options - 2];
#else
  // Android and mobile apps only has two modes: streching mode and
  // non-streching mode.
  const char* kSizeStr =
      main_window_->is_stretch_mode()
          ? GetLocalizedString(string_resources::IDR_IN_GAME_MENU_STRETCH)
                .c_str()
          : GetLocalizedString(string_resources::IDR_IN_GAME_MENU_ORIGINAL)
                .c_str();
#endif

  ScopedFont scoped_font = GetPreferredFont(context.font_size, kSizeStr);
  ImVec2 window_text_size = ImGui::CalcTextSize(kSizeStr);
  ImGui::SetCursorPosX(context.window_center_x + kMenuItemMargin +
                       (context.window_size.x / 2 - window_text_size.x) / 2);
  int text_y = ImGui::GetCursorPosY();
  ImGui::Text("%s", kSizeStr);

  AddRectForSettingsItem(
      SettingsItem::kWindowSize,
      SDL_Rect{context.window_center_x, text_y, context.window_center_x,
               static_cast<int>(ImGui::GetCursorPosY() - text_y)});

  if (settings_entered_ && current_setting_ == SettingsItem::kWindowSize) {
    float prompt_height = ImGui::GetFontSize();
    float prompt_width = prompt_height * .8f;

    ImVec2 triangle_p0(context.window_center_x + kMenuItemMargin +
                           prompt_width + kMenuItemMargin,
                       text_y + context.title_menu_height);

#if !KIWI_MOBILE
    bool has_no_left = context.window_scaling_for_options <= 2;
#if !KIWI_WASM
    bool has_right = !main_window_->is_fullscreen();
#else
    // Disable window settings. It should be handled by <canvas>.
    has_no_left = true;
    bool has_right = false;
#endif
#else
    bool has_no_left = !main_window_->is_stretch_mode();
    bool has_right = has_no_left;
#endif

    Triangle prompt_left{
        ImVec2(triangle_p0.x - prompt_width - context.options_items_spacing,
               triangle_p0.y + prompt_height / 2),
        ImVec2(triangle_p0.x - context.options_items_spacing, triangle_p0.y),
        ImVec2(triangle_p0.x - context.options_items_spacing,
               triangle_p0.y + prompt_height),
    };
    Triangle prompt_right{
        ImVec2(context.window_pos.x + context.window_size.x - kMenuItemMargin -
                   prompt_width,
               triangle_p0.y),
        ImVec2(context.window_pos.x + context.window_size.x - kMenuItemMargin -
                   prompt_width,
               triangle_p0.y + prompt_height),
        ImVec2(context.window_pos.x + context.window_size.x - kMenuItemMargin,
               triangle_p0.y + prompt_height / 2),
    };
    AddRectForSettingsItemPrompt(SettingsItem::kWindowSize,
                                 prompt_left.BoundingBox(),
                                 prompt_right.BoundingBox());
    DrawTrianglePrompt(!has_no_left, has_right, prompt_left, prompt_right);
  }
}

void InGameMenu::DrawMenuItemsImmediate_DrawMenuItems_Options_Joysticks(
    LayoutImmediateContext& context) {
  for (int j = 0; j < 2; ++j) {
    const int kJoyDescSpacing = 3 * main_window_->window_scale();
    const char* kStrNone =
        GetLocalizedString(string_resources::IDR_IN_GAME_MENU_NONE).c_str();
    ScopedFont joy_font(
        styles::in_game_menu::GetJoystickFontType(kStrNone, context.font_size));
    std::string joyname =
        std::string("P") + kiwi::base::NumberToString(j + 1) +
        std::string(": ") +
        (runtime_data_->joystick_mappings[j].which
             ? (SDL_GameControllerName(reinterpret_cast<SDL_GameController*>(
                   runtime_data_->joystick_mappings[j].which)))
             : kStrNone);
    ImVec2 window_text_size = ImGui::CalcTextSize(joyname.c_str());
    ImGui::SetCursorPosX(context.window_center_x + kMenuItemMargin +
                         (context.window_size.x / 2 - window_text_size.x) / 2);
    int text_y = ImGui::GetCursorPosY();
    ImGui::Text("%s", joyname.c_str());
    float prompt_height = ImGui::GetFontSize();
    float prompt_width = prompt_height * .8f;

    AddRectForSettingsItem(
        static_cast<SettingsItem>(j + static_cast<int>(SettingsItem::kJoyP1)),
        SDL_Rect{context.window_center_x, text_y, context.window_center_x,
                 static_cast<int>(ImGui::GetCursorPosY() - text_y)});

    if (settings_entered_ &&
            (j == 0 && current_setting_ == SettingsItem::kJoyP1) ||
        (j == 1 && current_setting_ == SettingsItem::kJoyP2)) {
      ImVec2 triangle_p0(context.window_center_x + kMenuItemMargin +
                             prompt_width + kMenuItemMargin,
                         text_y + context.title_menu_height);

      bool has_left = false, has_right = false;
      auto controllers = GetControllerList();
      auto target_iter =
          std::find(controllers.begin(), controllers.end(),
                    (reinterpret_cast<SDL_GameController*>(
                        runtime_data_->joystick_mappings[j].which)));
      has_left = (*target_iter != nullptr);
      has_right = target_iter != controllers.end() &&
                  target_iter < controllers.end() - 1;

      Triangle prompt_left{
          ImVec2(triangle_p0.x - prompt_width - context.options_items_spacing,
                 triangle_p0.y + prompt_height / 2),
          ImVec2(triangle_p0.x - context.options_items_spacing, triangle_p0.y),
          ImVec2(triangle_p0.x - context.options_items_spacing,
                 triangle_p0.y + prompt_height)};
      Triangle prompt_right{
          ImVec2(context.window_pos.x + context.window_size.x -
                     kMenuItemMargin - prompt_width,
                 triangle_p0.y),
          ImVec2(context.window_pos.x + context.window_size.x -
                     kMenuItemMargin - prompt_width,
                 triangle_p0.y + prompt_height),
          ImVec2(context.window_pos.x + context.window_size.x - kMenuItemMargin,
                 triangle_p0.y + prompt_height / 2)};
      AddRectForSettingsItemPrompt(current_setting_, prompt_left.BoundingBox(),
                                   prompt_right.BoundingBox());
      DrawTrianglePrompt(has_left, has_right, prompt_left, prompt_right);
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + kJoyDescSpacing);
  }
}

void InGameMenu::DrawMenuItemsImmediate_DrawMenuItems_Options_Languages(
    LayoutImmediateContext& context) {
  SupportedLanguage language = GetCurrentSupportedLanguage();
  const char* str_lang = nullptr;
  switch (language) {
#if !DISABLE_CHINESE_FONT
    case SupportedLanguage::kSimplifiedChinese:
      str_lang =
          GetLocalizedString(string_resources::IDR_IN_GAME_MENU_LANGUAGE_ZH)
              .c_str();
      break;
#endif
#if !DISABLE_JAPANESE_FONT
    case SupportedLanguage::kJapanese:
      str_lang =
          GetLocalizedString(string_resources::IDR_IN_GAME_MENU_LANGUAGE_JP)
              .c_str();
      break;
#endif
    case SupportedLanguage::kEnglish:
    default:
      str_lang =
          GetLocalizedString(string_resources::IDR_IN_GAME_MENU_LANGUAGE_EN)
              .c_str();
      break;
  }
  SDL_assert(str_lang);

  ScopedFont scoped_font = GetPreferredFont(context.font_size, str_lang);

  float prompt_height = ImGui::GetFontSize();
  float prompt_width = prompt_height * .8f;
  ImVec2 window_text_size = ImGui::CalcTextSize(str_lang);
  ImGui::SetCursorPosX(context.window_center_x + kMenuItemMargin +
                       (context.window_size.x / 2 - window_text_size.x) / 2);
  int text_y = ImGui::GetCursorPosY();
  ImGui::Text("%s", str_lang);

  AddRectForSettingsItem(
      SettingsItem::kLanguage,
      SDL_Rect{context.window_center_x, text_y, context.window_center_x,
               static_cast<int>(ImGui::GetCursorPosY() - text_y)});

  if (settings_entered_ && current_setting_ == SettingsItem::kLanguage) {
    ImVec2 triangle_p0(context.window_center_x + kMenuItemMargin +
                           prompt_width + kMenuItemMargin,
                       text_y + context.title_menu_height);
    constexpr bool kSupportChangeLanguage =
        static_cast<int>(SupportedLanguage::kMax) > 1;
    bool has_left = kSupportChangeLanguage, has_right = kSupportChangeLanguage;
    Triangle prompt_left{
        ImVec2(triangle_p0.x - prompt_width - context.options_items_spacing,
               triangle_p0.y + prompt_height / 2),
        ImVec2(triangle_p0.x - context.options_items_spacing, triangle_p0.y),
        ImVec2(triangle_p0.x - context.options_items_spacing,
               triangle_p0.y + prompt_height)};
    Triangle prompt_right{
        ImVec2(context.window_pos.x + context.window_size.x - kMenuItemMargin -
                   prompt_width,
               triangle_p0.y),
        ImVec2(context.window_pos.x + context.window_size.x - kMenuItemMargin -
                   prompt_width,
               triangle_p0.y + prompt_height),
        ImVec2(context.window_pos.x + context.window_size.x - kMenuItemMargin,
               triangle_p0.y + prompt_height / 2)};
    DrawTrianglePrompt(has_left, has_right, prompt_left, prompt_right);
    AddRectForSettingsItemPrompt(SettingsItem::kLanguage,
                                 prompt_left.BoundingBox(),
                                 prompt_right.BoundingBox());
  }
}

void InGameMenu::DrawSelectionImmediate(LayoutImmediateContext& context) {
  // Draw selection
  constexpr int kSelectionPadding = 3;
  int current_selection = static_cast<int>(current_selection_);
  ImVec2 selection_rect_pt0(
      0, context.menu_tops[current_selection] + context.title_menu_height);
  ImVec2 selection_rect_pt1(
      context.window_center_x - 1,
      context.menu_tops[current_selection] + context.title_menu_height);

  ImDrawList* bg_draw_list = ImGui::GetBackgroundDrawList();
  bg_draw_list->AddRectFilled(
      ImVec2(selection_rect_pt0.x, selection_rect_pt0.y - kSelectionPadding),
      ImVec2(selection_rect_pt1.x,
             selection_rect_pt1.y + kSelectionPadding + context.menu_font_size),
      IM_COL32_WHITE);

  // Register menu position to response finger touch/click events.
  // The responding area is exactly the 'draw-selection area'.
  if (menu_positions_.empty()) {
    int i = 0;
    for (const char* item : context.menu_items) {
      if (hide_menus_.find(i) != hide_menus_.end()) {
        // The current menu 'i' is hidden.
        ++i;
      } else {
        int menu_top = context.menu_tops[i];
        menu_positions_[static_cast<MenuItem>(i)] = SDL_Rect{
            0, menu_top - kSelectionPadding + context.title_menu_height,
            context.window_center_x - 1,
            2 * kSelectionPadding + context.menu_font_size +
                context.title_menu_height};
        ++i;
      }
    }
  }
}

void InGameMenu::AddRectForSettingsItemPrompt(
    SettingsItem settings_index,
    const SDL_Rect& rect_for_left_prompt,
    const SDL_Rect& rect_for_right_prompt) {
  settings_prompt_positions_[settings_index].push_back(std::make_pair(
      rect_for_left_prompt, MakePromptSettingsItemContext(true)));
  settings_prompt_positions_[settings_index].push_back(std::make_pair(
      rect_for_right_prompt, MakePromptSettingsItemContext(false)));
}

void InGameMenu::AddRectForSettingsItemPrompt(
    SettingsItem settings_index,
    const SDL_Rect& complex_item_rect) {
  settings_prompt_positions_[settings_index].push_back(std::make_pair(
      complex_item_rect, MakePromptSettingsItemComplex(complex_item_rect)));
}

void InGameMenu::AddRectForSettingsItem(SettingsItem settings_index,
                                        const SDL_Rect& rect) {
  settings_positions_[settings_index] = rect;
}

void InGameMenu::CleanUpSettingsItemRects() {
  settings_positions_.clear();
  settings_prompt_positions_.clear();
}

bool InGameMenu::HandleMouseOrFingerEvents(MouseOrFingerEventType type,
                                           int x_in_window,
                                           int y_in_window) {
  // Moving finger/mouse only affects selection. It won't trigger the menu item.
  bool suppress_trigger = type == MouseOrFingerEventType::kFingerMove;

  SDL_Rect bounds = window()->GetClientBounds();
  // Find if touched/clicked any menu item.
  for (const auto& menu : menu_positions_) {
    if (Contains(menu.second, x_in_window, y_in_window)) {
      if (settings_entered_)
        settings_entered_ = false;
      if (current_selection_ != menu.first)
        current_selection_ = menu.first;
      else if (!suppress_trigger)
        HandleMenuItemForCurrentSelection();
      return true;
    }
  }

  // Find if touched/clicked any settings item. Ignoring finger moving.
  if (type == MouseOrFingerEventType::kFingerDown ||
      type == MouseOrFingerEventType::kMousePressed) {
    // Find settings item first.
    for (const auto& setting : settings_positions_) {
      // If the setting hasn't been entered, enter the setting first.
      // If current setting item has already been selected, propagates the event
      // to its prompt.
      SDL_Rect setting_bounds_in_window = MapToWindow(setting.second);
      if (Contains(setting_bounds_in_window, x_in_window, y_in_window) &&
          (current_setting_ != setting.first || !settings_entered_)) {
        settings_entered_ = true;
        current_setting_ = setting.first;
        return true;
      }
    }

    // Find prompt.
    for (const auto& prompt_position : settings_prompt_positions_) {
      for (const auto& prompt : prompt_position.second) {
        // Prompt.first represents the button position
        // Prompt.second represents its context.
        if (Contains(prompt.first, x_in_window, y_in_window)) {
          current_setting_ = prompt_position.first;
          if (prompt.second.type ==
              SettingsItemContext::SettingsItemType::kPrompt) {
            HandleSettingsItemForCurrentSelection(prompt.second);
          } else {
            // Inserts bound information
            SettingsItemContext context = prompt.second;
            context.bounds.trigger_position =
                SDL_Point{x_in_window, y_in_window};
            HandleSettingsItemForCurrentSelection(context);
          }
          return true;
        }
      }
    }
  }

  return false;
}