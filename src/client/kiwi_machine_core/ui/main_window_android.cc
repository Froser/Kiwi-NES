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

#include "ui/main_window.h"

#include <SDL.h>

#include "ui/widgets/canvas.h"
#include "ui/widgets/joystick_button.h"
#include "ui/widgets/kiwi_items_widget.h"
#include "ui/widgets/touch_button.h"
#include "ui/widgets/virtual_joystick.h"

namespace {
constexpr int kDefaultWindowWidth = Canvas::kNESFrameDefaultWidth;
constexpr int kDefaultWindowHeight = Canvas::kNESFrameDefaultHeight;
}  // namespace

bool MainWindow::IsLandscape() {
  const SDL_Rect kClientBounds = GetClientBounds();
  return kClientBounds.w > kClientBounds.h;
}

void MainWindow::CreateVirtualTouchButtons() {
  {
    std::unique_ptr<VirtualJoystick> vtb_joystick =
        std::make_unique<VirtualJoystick>(this);
    vtb_joystick_ = vtb_joystick.get();
    vtb_joystick->set_visible(false);
    vtb_joystick->set_joystick_callback(kiwi::base::BindRepeating(
        &MainWindow::OnVirtualJoystickChanged, kiwi::base::Unretained(this)));
    AddWidget(std::move(vtb_joystick));
  }

  {
    std::unique_ptr<JoystickButton> vtb_a =
        std::make_unique<JoystickButton>(this, image_resources::ImageID::kVtbA);
    vtb_a_ = vtb_a.get();
    vtb_a->set_finger_down_callback(kiwi::base::BindRepeating(
        &MainWindow::SetVirtualJoystickButton, kiwi::base::Unretained(this), 0,
        kiwi::nes::ControllerButton::kA, true));
    vtb_a->set_trigger_callback(kiwi::base::BindRepeating(
        &MainWindow::SetVirtualJoystickButton, kiwi::base::Unretained(this), 0,
        kiwi::nes::ControllerButton::kA, false));
    vtb_a->set_visible(false);
    AddWidget(std::move(vtb_a));
  }

  {
    std::unique_ptr<JoystickButton> vtb_b =
        std::make_unique<JoystickButton>(this, image_resources::ImageID::kVtbB);
    vtb_b_ = vtb_b.get();
    vtb_b->set_finger_down_callback(kiwi::base::BindRepeating(
        &MainWindow::SetVirtualJoystickButton, kiwi::base::Unretained(this), 0,
        kiwi::nes::ControllerButton::kB, true));
    vtb_b->set_trigger_callback(kiwi::base::BindRepeating(
        &MainWindow::SetVirtualJoystickButton, kiwi::base::Unretained(this), 0,
        kiwi::nes::ControllerButton::kB, false));
    vtb_b->set_visible(false);
    AddWidget(std::move(vtb_b));
  }

  {
    std::unique_ptr<JoystickButton> vtb_ab = std::make_unique<JoystickButton>(
        this, image_resources::ImageID::kVtbAb);
    vtb_ab_ = vtb_ab.get();
    vtb_ab->set_finger_down_callback(
        kiwi::base::BindRepeating(&MainWindow::SetVirtualJoystickButton,
                                  kiwi::base::Unretained(this), 0,
                                  kiwi::nes::ControllerButton::kA, true)
            .Then(kiwi::base::BindRepeating(
                &MainWindow::SetVirtualJoystickButton,
                kiwi::base::Unretained(this), 0,
                kiwi::nes::ControllerButton::kB, true)));
    vtb_ab->set_trigger_callback(
        kiwi::base::BindRepeating(&MainWindow::SetVirtualJoystickButton,
                                  kiwi::base::Unretained(this), 0,
                                  kiwi::nes::ControllerButton::kA, false)
            .Then(kiwi::base::BindRepeating(
                &MainWindow::SetVirtualJoystickButton,
                kiwi::base::Unretained(this), 0,
                kiwi::nes::ControllerButton::kB, false)));
    vtb_ab->set_visible(false);
    AddWidget(std::move(vtb_ab));
  }

  {
    std::unique_ptr<JoystickButton> vtb_b =
        std::make_unique<JoystickButton>(this, image_resources::ImageID::kVtbB);
    vtb_b_ = vtb_b.get();
    vtb_b->set_finger_down_callback(kiwi::base::BindRepeating(
        &MainWindow::SetVirtualJoystickButton, kiwi::base::Unretained(this), 0,
        kiwi::nes::ControllerButton::kB, true));
    vtb_b->set_trigger_callback(kiwi::base::BindRepeating(
        &MainWindow::SetVirtualJoystickButton, kiwi::base::Unretained(this), 0,
        kiwi::nes::ControllerButton::kB, false));
    vtb_b->set_visible(false);
    AddWidget(std::move(vtb_b));
  }

  {
    constexpr float kScaling = .4f;
    {
      std::unique_ptr<TouchButton> vtb_select = std::make_unique<TouchButton>(
          this, image_resources::ImageID::kVtbSelect);
      vtb_select_ = vtb_select.get();
      vtb_select->set_finger_down_callback(kiwi::base::BindRepeating(
          &MainWindow::SetVirtualJoystickButton, kiwi::base::Unretained(this),
          0, kiwi::nes::ControllerButton::kSelect, true));
      vtb_select->set_trigger_callback(kiwi::base::BindRepeating(
          &MainWindow::SetVirtualJoystickButton, kiwi::base::Unretained(this),
          0, kiwi::nes::ControllerButton::kSelect, false));
      SDL_Rect bounds = vtb_select->bounds();
      bounds.w *= window_scale() * kScaling;
      bounds.h *= window_scale() * kScaling;
      vtb_select->set_opacity(.3f);
      vtb_select->set_bounds(bounds);
      vtb_select->set_visible(false);
      AddWidget(std::move(vtb_select));
    }

    {
      std::unique_ptr<TouchButton> vtb_start = std::make_unique<TouchButton>(
          this, image_resources::ImageID::kVtbStart);
      vtb_start_ = vtb_start.get();
      vtb_start->set_finger_down_callback(kiwi::base::BindRepeating(
          &MainWindow::SetVirtualJoystickButton, kiwi::base::Unretained(this),
          0, kiwi::nes::ControllerButton::kStart, true));
      vtb_start->set_trigger_callback(kiwi::base::BindRepeating(
          &MainWindow::SetVirtualJoystickButton, kiwi::base::Unretained(this),
          0, kiwi::nes::ControllerButton::kStart, false));
      SDL_Rect bounds = vtb_start->bounds();
      bounds.w *= window_scale() * kScaling;
      bounds.h *= window_scale() * kScaling;
      vtb_start->set_opacity(.3f);
      vtb_start->set_bounds(bounds);
      vtb_start->set_visible(false);
      AddWidget(std::move(vtb_start));
    }

    {
      std::unique_ptr<TouchButton> vtb_pause = std::make_unique<TouchButton>(
          this, image_resources::ImageID::kVtbPause);
      vtb_pause_ = vtb_pause.get();
      vtb_pause->set_trigger_callback(kiwi::base::BindRepeating(
          &MainWindow::OnInGameMenuTrigger, kiwi::base::Unretained(this)));
      vtb_pause->set_visible(false);
      AddWidget(std::move(vtb_pause));
    }
  }
}

void MainWindow::SetVirtualTouchButtonVisible(VirtualTouchButton button,
                                              bool visible) {
  switch (button) {
    case VirtualTouchButton::kStart:
      if (vtb_start_)
        vtb_start_->set_visible(visible);
      break;
    case VirtualTouchButton::kSelect:
      if (vtb_select_)
        vtb_select_->set_visible(visible);
      break;
    case VirtualTouchButton::kJoystick:
      if (vtb_joystick_)
        vtb_joystick_->set_visible(visible);
      break;
    case VirtualTouchButton::kA:
      if (vtb_a_)
        vtb_a_->set_visible(visible);
      break;
    case VirtualTouchButton::kB:
      if (vtb_b_)
        vtb_b_->set_visible(visible);
      break;
    case VirtualTouchButton::kAB:
      if (vtb_ab_)
        vtb_ab_->set_visible(visible);
      break;
    case VirtualTouchButton::kPause:
      if (vtb_pause_)
        vtb_pause_->set_visible(visible);
      break;
    default:
      SDL_assert(false);
      break;
  }
}

void MainWindow::LayoutVirtualTouchButtons() {
  const SDL_Rect kClientBounds = GetClientBounds();
  bool is_landscape = IsLandscape();

  {
    const int kSize = 135 * window_scale();
    const int kPaddingX =
        is_landscape ? 18 * window_scale() : 10 * window_scale();
    const int kPaddingY =
        is_landscape ? 18 * window_scale() : 40 * window_scale();

    if (vtb_joystick_) {
      SDL_Rect bounds;
      bounds.h = bounds.w = kSize;
      bounds.x = kPaddingX;
      bounds.y = kClientBounds.h - bounds.h - kPaddingY;
      vtb_joystick_->set_bounds(bounds);
    }
  }

  {
    const int kSize = 33 * window_scale();
    const int kPaddingX =
        is_landscape ? 60 * window_scale() : 30 * window_scale();
    const int kPaddingY =
        is_landscape ? 60 * window_scale() : 80 * window_scale();
    const int kSpacing = 15 * window_scale();
    if (vtb_a_) {
      SDL_Rect bounds;
      bounds.h = bounds.w = kSize;
      bounds.x = kClientBounds.w - bounds.w - kPaddingX;
      bounds.y = kClientBounds.h - bounds.h - kPaddingY;
      vtb_a_->set_bounds(bounds);
    }

    if (vtb_b_) {
      SDL_Rect bounds;
      bounds.h = bounds.w = kSize;
      bounds.x = kClientBounds.w - bounds.w * 2 - kPaddingX - kSpacing;
      bounds.y = kClientBounds.h - bounds.h - kPaddingY;
      vtb_b_->set_bounds(bounds);
    }

    if (vtb_ab_) {
      SDL_Rect bounds;
      bounds.h = bounds.w = kSize;
      bounds.x = kClientBounds.w - bounds.w - kPaddingX;
      bounds.y = kClientBounds.h - bounds.h * 2 - kPaddingY - kSpacing;
      vtb_ab_->set_bounds(bounds);
    }
  }

  {
    const int kMiddleSpacing = 4 * window_scale();
    const int kPaddingBottom =
        is_landscape ? 30 * window_scale() : 10 * window_scale();
    if (vtb_select_) {
      SDL_Rect bounds = vtb_select_->bounds();
      bounds.x = kClientBounds.w / 2 - bounds.w - kMiddleSpacing;
      bounds.y = kClientBounds.h - bounds.h - kPaddingBottom;
      vtb_select_->set_bounds(bounds);
    }

    if (vtb_start_) {
      SDL_Rect bounds = vtb_select_->bounds();
      bounds.x = kClientBounds.w / 2 + kMiddleSpacing;
      bounds.y = kClientBounds.h - bounds.h - kPaddingBottom;
      vtb_start_->set_bounds(bounds);
    }
  }

  if (vtb_pause_) {
    const int kPadding = 33 * window_scale();
    const int kSize = 33 * window_scale();
    SDL_Rect bounds;
    bounds.h = bounds.w = kSize;
    bounds.x = bounds.y = kPadding;
    vtb_pause_->set_bounds(bounds);
  }
}

void MainWindow::OnVirtualJoystickChanged(int state) {
  SetVirtualJoystickButton(0, kiwi::nes::ControllerButton::kLeft, false);
  SetVirtualJoystickButton(0, kiwi::nes::ControllerButton::kRight, false);
  SetVirtualJoystickButton(0, kiwi::nes::ControllerButton::kUp, false);
  SetVirtualJoystickButton(0, kiwi::nes::ControllerButton::kDown, false);

  if (state & VirtualJoystick::kLeft)
    SetVirtualJoystickButton(0, kiwi::nes::ControllerButton::kLeft, true);
  if (state & VirtualJoystick::kRight)
    SetVirtualJoystickButton(0, kiwi::nes::ControllerButton::kRight, true);
  if (state & VirtualJoystick::kUp)
    SetVirtualJoystickButton(0, kiwi::nes::ControllerButton::kUp, true);
  if (state & VirtualJoystick::kDown)
    SetVirtualJoystickButton(0, kiwi::nes::ControllerButton::kDown, true);
}

void MainWindow::OnInGameSettingsHandleWindowSize(bool is_left) {
  if (config_->data().is_stretch_mode && !is_left)
    return;

  if (config_->data().is_stretch_mode && is_left) {
    config_->data().is_stretch_mode = false;
    config_->SaveConfig();
    OnScaleModeChanged();
  } else if (!config_->data().is_stretch_mode && !is_left) {
    config_->data().is_stretch_mode = true;
    config_->SaveConfig();
    OnScaleModeChanged();
  }
}

void MainWindow::OnScaleModeChanged() {
  if (canvas_) {
    bool is_landscape = IsLandscape();
    if (!is_landscape) {
      const int kPadding = 80 * window_scale();
      SDL_Rect canvas_bounds = canvas_->bounds();
      canvas_bounds.y = kPadding;
      canvas_->set_bounds(canvas_bounds);
    }

    float canvas_scale = 2.f;
    if (config_->data().is_stretch_mode) {
      SDL_Rect rect = GetClientBounds();
      if (is_landscape) {
        canvas_scale = static_cast<float>(rect.h) / kDefaultWindowWidth;
      } else {
        canvas_scale = static_cast<float>(rect.w) / kDefaultWindowHeight;
      }
    }
    canvas_->set_frame_scale(canvas_scale);
  }
}

void MainWindow::OnAboutToRenderFrame(Canvas* canvas,
                                      scoped_refptr<NESFrame> frame) {
  SDL_Rect dest_rect;
  if (IsLandscape()) {
    // Always adjusts the canvas to the middle of the render area (excludes menu
    // bar).
    SDL_Rect render_bounds = GetClientBounds();
    SDL_Rect src_rect = {0, 0, frame->width(), frame->height()};
    dest_rect = {
        static_cast<int>(
            (render_bounds.w - src_rect.w * canvas->frame_scale()) / 2) +
            render_bounds.x,
        static_cast<int>(
            (render_bounds.h - src_rect.h * canvas->frame_scale()) / 2) +
            render_bounds.y,
        static_cast<int>(frame->width() * canvas->frame_scale()),
        static_cast<int>(frame->height() * canvas->frame_scale())};
    canvas->set_bounds(dest_rect);
  } else {
    dest_rect = {canvas->bounds().x, canvas->bounds().y,
                 static_cast<int>(frame->width() * canvas->frame_scale()),
                 static_cast<int>(frame->height() * canvas->frame_scale())};
    canvas->set_bounds(dest_rect);
  }

  // Updates window size, to fit the frame.
  if (menu_bar_) {
    SDL_Rect menu_rect = menu_bar_->bounds();
    int desired_window_width = dest_rect.w;
    int desired_window_height = menu_rect.h + dest_rect.h;
    Resize(desired_window_width, desired_window_height);
  } else {
    Resize(dest_rect.w, dest_rect.h);
  }
}

void MainWindow::OnInGameSettingsHandleVolume(bool is_left) {
  OnSetAudioVolume(is_left ? 0.f : 1.f);
}
