// Copyright (C) 2024 Yisi Yu
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

#include "ui/widgets/flex_items_widget.h"

#include <imgui.h>
#include <set>

#include "ui/main_window.h"
#include "ui/widgets/flex_item_widget.h"
#include "utility/audio_effects.h"
#include "utility/key_mapping_util.h"
#include "utility/math.h"

namespace {
constexpr int kItemHeightHint = 160;
constexpr int kItemSelectedHighlightedSize = 20;
constexpr int kItemAnimationMs = 50;
constexpr int kScrollingAnimationMs = 20;

int CalculateIntersectionArea(const SDL_Rect& lhs, const SDL_Rect& rhs) {
  SDL_assert(lhs.h == rhs.h);
  int lhs_x2 = lhs.x + lhs.w;
  int rhs_x2 = rhs.x + rhs.w;

  return std::min(lhs_x2, rhs_x2) - std::max(lhs.x, rhs.x);
}

}  // namespace

FlexItemsWidget::FlexItemsWidget(MainWindow* main_window,
                                 NESRuntimeID runtime_id)
    : Widget(main_window), main_window_(main_window) {
  runtime_data_ = NESRuntime::GetInstance()->GetDataById(runtime_id);
  SDL_assert(runtime_data_);
  set_flags(ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);
  set_title("KiwiItemsWidget");
}

FlexItemsWidget::~FlexItemsWidget() = default;

size_t FlexItemsWidget::AddItem(
    std::unique_ptr<LocalizedStringUpdater> title_updater,
    const kiwi::nes::Byte* cover_img_ref,
    size_t cover_size,
    kiwi::base::RepeatingClosure on_trigger) {
  std::unique_ptr<FlexItemWidget> item = std::make_unique<FlexItemWidget>(
      main_window_, this, std::move(title_updater), on_trigger);

  item->set_cover(cover_img_ref, cover_size);
  items_.push_back(item.get());
  AddWidget(std::move(item));
  need_layout_all_ = true;

  return items_.size() - 1;
}

void FlexItemsWidget::AddSubItem(
    size_t item_index,
    std::unique_ptr<LocalizedStringUpdater> title_updater,
    const kiwi::nes::Byte* cover_img_ref,
    size_t cover_size,
    kiwi::base::RepeatingClosure on_trigger) {
  SDL_assert(item_index < items_.size());
  items_[item_index]->AddSubItem(std::move(title_updater), cover_img_ref,
                                 cover_size, on_trigger);
}

void FlexItemsWidget::SetIndex(size_t index) {
  SetIndex(index, LayoutOption::kAdjustScrolling);
}

void FlexItemsWidget::SetIndex(size_t index, LayoutOption option) {
  if (current_index_ != index) {
    current_index_ = index;
    if (items_.empty())
      current_index_ = 0;
    else if (current_index_ >= items_.size())
      current_index_ = items_.size() - 1;
    Layout(option);
  }
}

bool FlexItemsWidget::IsItemSelected(FlexItemWidget* item) {
  SDL_assert(current_index_ < items_.size());
  return items_[current_index_] == item;
}

void FlexItemsWidget::SetActivate(bool activate) {
  if (activate_ != activate) {
    activate_ = activate;
    Layout(LayoutOption::kDoNotAdjustScrolling);
  }
}

void FlexItemsWidget::ScrollWith(int scrolling_delta,
                                 int mouse_x,
                                 int mouse_y) {
  // Scrolling
  target_view_scrolling_ += scrolling_delta;

  // Avoid exceeding the top of the widget
  if (target_view_scrolling_ >= 0)
    target_view_scrolling_ = 0;

  // Avoid exceeding the bottom of the widget
  FlexItemWidget* last_item = items_[items_.size() - 1];
  SDL_Rect last_item_absolute_bounds = bounds_map_without_scrolling_[last_item];
  if (last_item_absolute_bounds.y + last_item_absolute_bounds.h +
          target_view_scrolling_ <
      bounds().h) {
    target_view_scrolling_ =
        bounds().h - last_item_absolute_bounds.h - last_item_absolute_bounds.y;
  }

  updating_view_scrolling_ = true;

  // Highlight
  size_t item_index;
  bool current_index_exceeded_bottom;
  if (FindItemIndexByMousePosition(mouse_x, mouse_y, item_index)) {
    current_index_exceeded_bottom =
        HighlightItem(items_[item_index], LayoutOption::kDoNotAdjustScrolling);
  } else {
    current_index_exceeded_bottom = HighlightItem(
        current_item_widget_, LayoutOption::kDoNotAdjustScrolling);
  }
  if (current_index_exceeded_bottom)
    AdjustBottomRowItemsIfNeeded(LayoutOption::kDoNotAdjustScrolling);
}

void FlexItemsWidget::Layout(LayoutOption option) {
  if (need_layout_all_)
    LayoutAll(option);
  else
    LayoutPartial(option);
}

void FlexItemsWidget::LayoutAll(LayoutOption option) {
  const SDL_Rect kLocalBounds = GetLocalBounds();
  if (SDL_RectEmpty(&kLocalBounds))
    return;

  original_view_scrolling_ = target_view_scrolling_;
  int anchor_x = 0, anchor_y = 0;
  int column_index = 0, row_index = 0;
  size_t index = 0;
  rows_to_first_item_.clear();
  rows_to_first_item_[0] = 0;

  bool current_index_exceeded_bottom = false;
  for (auto* item : items_) {
    bool is_selected = (index == current_index_);
    SDL_Rect item_bounds = item->GetSuggestedSize(kItemHeightHint, is_selected);
    if (anchor_x + item_bounds.w > bounds().w) {
      anchor_y += item_bounds.h;
      anchor_x = 0;
      row_index++;
      column_index = 0;
      rows_to_first_item_[row_index] = index;
    } else {
      column_index++;
    }

    item->set_row_index(row_index);
    item->set_column_index(column_index);

    item_bounds.x = anchor_x;
    item_bounds.y = anchor_y;
    anchor_x += item_bounds.w;

    bounds_map_without_scrolling_[item] = item_bounds;
    if (IsItemSelected(item)) {
      current_index_exceeded_bottom = HighlightItem(item, option);
    }

    if (target_view_scrolling_ == 0) {
      // Applying scrolling above won't set visibility when view_scrolling_ is
      // zero. So we set it here.
      item->set_visible(SDL_HasIntersection(&item_bounds, &kLocalBounds));
    }

    index++;
  }

  // Updates max row index:
  rows_ = row_index;

  // If the current index is the last row, viewport need to be adjusted.
  if (current_index_exceeded_bottom)
    AdjustBottomRowItemsIfNeeded(option);

  ResetAnimationTimers();
  need_layout_all_ = false;
}

void FlexItemsWidget::LayoutPartial(LayoutOption option) {
  if (current_item_widget_ != items_[current_index_]) {
    original_view_scrolling_ = target_view_scrolling_;
    bool current_index_exceeded_bottom =
        HighlightItem(items_[current_index_], option);
    // If the current index is the last row, viewport need to be adjusted.
    if (current_index_exceeded_bottom)
      AdjustBottomRowItemsIfNeeded(option);

    ResetAnimationTimers();
  }
}

bool FlexItemsWidget::HighlightItem(FlexItemWidget* item, LayoutOption option) {
  bool current_index_exceeded_bottom = false;
  current_item_widget_ = item;

  SDL_Rect item_target_bounds = bounds_map_without_scrolling_[item];
  current_item_original_bounds_ = item_target_bounds;

  if (item_target_bounds.x == 0) {
    item_target_bounds.w += kItemSelectedHighlightedSize;
  } else if (item_target_bounds.x + item_target_bounds.w +
                 kItemSelectedHighlightedSize >
             bounds().w) {
    item_target_bounds.x -= kItemSelectedHighlightedSize;
  } else {
    item_target_bounds.x -= kItemSelectedHighlightedSize;
    item_target_bounds.w += kItemSelectedHighlightedSize * 2;
  }

  if (item_target_bounds.y == 0) {
    item_target_bounds.h += kItemSelectedHighlightedSize;
  } else {
    item_target_bounds.y -= kItemSelectedHighlightedSize;
    item_target_bounds.h += kItemSelectedHighlightedSize * 2;
  }

  if (target_view_scrolling_ + item_target_bounds.y + item_target_bounds.h >
      bounds().h) {
    if (option == LayoutOption::kAdjustScrolling) {
      target_view_scrolling_ =
          bounds().h - (item_target_bounds.y + item_target_bounds.h);
    }
    current_index_exceeded_bottom = true;
  } else if (target_view_scrolling_ + item_target_bounds.y < 0 &&
             option == LayoutOption::kAdjustScrolling) {
    target_view_scrolling_ = -item_target_bounds.y;
  }

  current_item_original_bounds_.y += target_view_scrolling_;
  current_item_target_bounds_ = item_target_bounds;
  current_item_target_bounds_.y += target_view_scrolling_;

  return current_index_exceeded_bottom;
}

void FlexItemsWidget::ResetAnimationTimers() {
  selection_item_timer_.Reset();
  scrolling_timer_.Reset();
  updating_view_scrolling_ = true;
}

void FlexItemsWidget::AdjustBottomRowItemsIfNeeded(LayoutOption option) {
  if (current_item_widget_ && current_item_widget_->row_index() == rows_) {
    if (option == LayoutOption::kAdjustScrolling) {
      target_view_scrolling_ += kItemSelectedHighlightedSize;
      current_item_target_bounds_.h -= kItemSelectedHighlightedSize;
      current_item_target_bounds_.y += kItemSelectedHighlightedSize;
      current_item_original_bounds_.y += kItemSelectedHighlightedSize;
    } else {
      current_item_target_bounds_.h -= kItemSelectedHighlightedSize;
    }
  }
}

bool FlexItemsWidget::HandleInputEvent(SDL_KeyboardEvent* k,
                                       SDL_ControllerButtonEvent* c) {
  if (!activate_)
    return false;

  if (k && k->keysym.mod) {
    // If any modifier is pressed, we won't treat this key event as handled,
    // because many shortcut such as 'Command+W' will close the application.
    return true;
  }

  // if (!is_finger_down_) { TODO
  if (IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kLeft, k) ||
      c && c->button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
    size_t next_index = FindNextIndex(kLeft);
    if (next_index != current_index_) {
      PlayEffect(audio_resources::AudioID::kSelect);
      SetIndex(next_index);
    } else {
      back_callback_.Run();
    }
    return true;
  }

  if (IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kB, k) ||
      c && c->button == SDL_CONTROLLER_BUTTON_B) {
    back_callback_.Run();
  }

  if (IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kRight, k) ||
      c && c->button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
    size_t next_index = FindNextIndex(kRight);
    if (next_index != current_index_) {
      PlayEffect(audio_resources::AudioID::kSelect);
      SetIndex(next_index);
    }
    return true;
  }

  if (IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kUp, k) ||
      c && c->button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
    size_t next_index = FindNextIndex(kUp);
    if (next_index != current_index_) {
      PlayEffect(audio_resources::AudioID::kSelect);
      SetIndex(next_index);
    }
    return true;
  }

  if (IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kDown, k) ||
      c && c->button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
    size_t next_index = FindNextIndex(kDown);
    if (next_index != current_index_) {
      PlayEffect(audio_resources::AudioID::kSelect);
      SetIndex(next_index);
    }
    return true;
  }

  if (IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kStart, k) ||
      c && c->button == SDL_CONTROLLER_BUTTON_START ||
      IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kA, k) ||
      c && c->button == SDL_CONTROLLER_BUTTON_A) {
    PlayEffect(audio_resources::AudioID::kStart);
    TriggerCurrentItem();
    return true;
  }

  if (IsKeyboardOrControllerAxisMotionMatch(
          runtime_data_, kiwi::nes::ControllerButton::kSelect, k)) {
    if (items_[current_index_]->has_sub_items()) {
      PlayEffect(audio_resources::AudioID::kSelect);
      items_[current_index_]->SwapToNextSubItem();
    }
    return true;
  }

  return false;
}

void FlexItemsWidget::TriggerCurrentItem() {
  items_[current_index_]->Trigger();
}

void FlexItemsWidget::ApplyScrolling(int scrolling) {
  const SDL_Rect kLocalBounds = GetLocalBounds();
  if (SDL_RectEmpty(&kLocalBounds))
    return;

  for (auto* item : items_) {
    SDL_Rect bounds = bounds_map_without_scrolling_[item];
    bounds.y += scrolling;
    item->set_bounds(bounds);
    item->set_visible(SDL_HasIntersection(&bounds, &kLocalBounds));
  }
}

size_t FlexItemsWidget::FindNextIndex(Direction direction) {
  switch (direction) {
    case kUp:
      return FindNextIndex(false);
    case kDown:
      return FindNextIndex(true);
    case kLeft: {
      if (current_index_ == 0)
        return 0;

      size_t next_index_candidate = current_index_ - 1;
      return (items_[next_index_candidate]->row_index() !=
              items_[current_index_]->row_index())
                 ? current_index_
                 : next_index_candidate;
    }
    case kRight: {
      if (current_index_ == items_.size() - 1)
        return items_.size() - 1;

      size_t next_index_candidate = current_index_ + 1;
      return (items_[next_index_candidate]->row_index() !=
              items_[current_index_]->row_index())
                 ? current_index_
                 : next_index_candidate;
    }
    default:
      SDL_assert(false);  // Shouldn't be here
      return 0;
  }
}

size_t FlexItemsWidget::FindNextIndex(bool down) {
  int area = 0, last_area = 0;
  int start_index, end_index;
  const int kCurrentRow = items_[current_index_]->row_index();
  if (down) {
    if (kCurrentRow == rows_)
      return current_index_;

    SDL_assert(kCurrentRow < rows_);
    start_index = rows_to_first_item_[kCurrentRow + 1];
    if (kCurrentRow < rows_ - 1)
      end_index = rows_to_first_item_[kCurrentRow + 2] - 1;
    else
      end_index = items_.size() - 1;
  } else {
    if (kCurrentRow == 0)
      return current_index_;

    SDL_assert(kCurrentRow > 0);
    start_index = rows_to_first_item_[kCurrentRow - 1];
    end_index = rows_to_first_item_[kCurrentRow] - 1;
  }

  int target_index = end_index;
  for (int i = start_index; i <= end_index; ++i) {
    SDL_assert(i >= 0 && i <= items_.size());
    int intersection_area =
        CalculateIntersectionArea(bounds_map_without_scrolling_[items_[i]],
                                  current_item_original_bounds_);

    if (intersection_area < 0)
      continue;

    area = intersection_area;
    if (area > last_area) {
      last_area = area;
      target_index = i;
    }
  }
  return target_index;
}

bool FlexItemsWidget::FindItemIndexByMousePosition(int x_in_window,
                                                   int y_in_window,
                                                   size_t& index_out) {
  // A faster way to find the hovered item.
  int current_row_index = 0;
  if (current_item_widget_) {
    SDL_assert(!rows_to_first_item_.empty());
    current_row_index = current_item_widget_->row_index();
  }

  int r = current_row_index;
  int first_item_of_row = rows_to_first_item_[r];
  while (first_item_of_row > 0 &&
         (items_[first_item_of_row]->bounds().y +
          items_[first_item_of_row]->bounds().h) >= 0) {
    --r;
    first_item_of_row = rows_to_first_item_[r];
  }
  int index_lower = first_item_of_row;

  r = current_row_index;
  first_item_of_row = rows_to_first_item_[r];
  bool broke = false;
  while (items_[first_item_of_row]->bounds().y <= bounds().h) {
    ++r;
    if (rows_to_first_item_.find(r) == rows_to_first_item_.end()) {
      broke = true;
      break;
    }
    first_item_of_row = rows_to_first_item_[r];
  }
  int index_upper = broke ? items_.size() - 1 : first_item_of_row;

  for (int i = index_lower; i <= index_upper; ++i) {
    SDL_Rect item_bounds_to_window = MapToWindow(items_[i]->bounds());
    if (Contains(item_bounds_to_window, x_in_window, y_in_window)) {
      index_out = i;
      return true;
    }
  }

  return false;
}

void FlexItemsWidget::Paint() {
  if (first_paint_) {
    Layout(LayoutOption::kAdjustScrolling);
    first_paint_ = false;
  }

  // Scrolling animation
  if (updating_view_scrolling_) {
    float percentage = scrolling_timer_.ElapsedInMilliseconds() /
                       static_cast<float>(kScrollingAnimationMs);
    if (percentage > 1.f) {
      percentage = 1.f;
      updating_view_scrolling_ = false;
    }
    int scrolling =
        Lerp(original_view_scrolling_, target_view_scrolling_, percentage);
    ApplyScrolling(scrolling);
  }

  // Selected item animation
  if (current_item_widget_) {
    float percentage = selection_item_timer_.ElapsedInMilliseconds() /
                       static_cast<float>(kItemAnimationMs);
    if (percentage > 1.f)
      percentage = 1.f;
    current_item_widget_->set_bounds(Lerp(current_item_original_bounds_,
                                          current_item_target_bounds_,
                                          percentage));
  }
}

void FlexItemsWidget::PostPaint() {
  // Renders current item again, to make it on the top of all the items.
  if (current_item_widget_) {
    current_item_widget_->Render();
  }

  if (!activate_) {
    SDL_Rect bounds_to_window = MapToWindow(bounds());
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(bounds_to_window.x, bounds_to_window.y),
        ImVec2(bounds_to_window.x + bounds_to_window.w,
               bounds_to_window.y + bounds_to_window.h),
        ImColor(0, 0, 0, 196));
  }
}

void FlexItemsWidget::OnWindowResized() {
  need_layout_all_ = true;
  Layout(LayoutOption::kAdjustScrolling);
}

bool FlexItemsWidget::OnKeyPressed(SDL_KeyboardEvent* event) {
  return HandleInputEvent(event, nullptr);
}

bool FlexItemsWidget::OnMouseMove(SDL_MouseMotionEvent* event) {
  if (!activate_ || mouse_locked_)
    return true;

  size_t index = 0;
  if (FindItemIndexByMousePosition(event->x, event->y, index))
    SetIndex(index, LayoutOption::kDoNotAdjustScrolling);

  return true;
}

bool FlexItemsWidget::OnMouseWheel(SDL_MouseWheelEvent* event) {
  if (!activate_ || mouse_locked_)
    return true;

  constexpr int kScrollingTurbo = 5;
  if (!items_.empty()) {
    const int kScrollingChangedValue = event->preciseY * kScrollingTurbo;
    ScrollWith(kScrollingChangedValue, event->mouseX, event->mouseY);
  }

  return true;
}

bool FlexItemsWidget::OnMousePressed(SDL_MouseButtonEvent* event) {
  if (!activate_)
    return true;

  mouse_locked_ = true;
  return true;
}

bool FlexItemsWidget::OnMouseReleased(SDL_MouseButtonEvent* event) {
  if (activate_) {
    mouse_locked_ = false;

    if (event->button == SDL_BUTTON_LEFT) {
      size_t index_before_released = current_index_;
      size_t index = 0;
      if (FindItemIndexByMousePosition(event->x, event->y, index)) {
        // If the highlight item doesn't change, trigger it.
        if (index_before_released == index) {
          PlayEffect(audio_resources::AudioID::kStart);
          TriggerCurrentItem();
        } else {
          SetIndex(index, LayoutOption::kDoNotAdjustScrolling);
        }
      }
    } else if (event->button == SDL_BUTTON_RIGHT) {
      back_callback_.Run();
    }
  } else {
    main_window_->ChangeFocus(MainWindow::MainFocus::kContents);
  }
  return true;
}

bool FlexItemsWidget::OnControllerButtonPressed(
    SDL_ControllerButtonEvent* event) {
  return HandleInputEvent(nullptr, event);
}

bool FlexItemsWidget::OnControllerAxisMotionEvent(
    SDL_ControllerAxisEvent* event) {
  return HandleInputEvent(nullptr, nullptr);
}

void FlexItemsWidget::OnWindowPreRender() {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
}

void FlexItemsWidget::OnWindowPostRender() {
  ImGui::PopStyleVar(2);
}

bool FlexItemsWidget::ChildrenAcceptHitTest() {
  // Children don't accept any hit test, and mouse events. All mouse events are
  // handled by this FlexItemsWidget.
  return false;
}