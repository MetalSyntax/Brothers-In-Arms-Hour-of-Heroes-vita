/**
 * @file input.h
 * @brief Vita front touch and buttons injected into the game's CTouchPad.
 */
#pragma once

/** @brief Starts touch/analog sampling and loads (or writes) controls.txt. */
void input_init(void);

/** @brief Resolves the CTouchPad entry points; call after macho_load(). */
void input_bind(void);

/** @brief Samples touch + pad and forwards changes to CTouchPad::EventTouchDown/Move/Up. */
void input_poll(void);
