typedef struct Combo {
  ComboKey array[COMBO_MAX_SIZE];
  uint8_t size;
  uint8_t state;
  uint32_t last_modify_time;
} Combo;

// #define COMBO_DEBUG

#ifdef COMBO_DEBUG
  #define TRANSITION_DEBUG(a) uprintf("transition '" #a "' now it is #%d: {", combo - &combo_stack[0]); \
    for (int i = 0; i < combo->size; ++i) { \
      uprintf("%d, ", combo->array[i]); \
    } \
    uprintf("} in %d\n", combo->state);
#else
  #define TRANSITION_DEBUG(a) ;
#endif

Combo combo_stack[COMBO_STACK_MAX_SIZE] = {};
uint8_t combo_stack_size = 0;
bool combo_enabled = true;
bool combo_k_enabled = true;

bool combo_is_combo_key(uint16_t key) {
  return CMB_000 <= key && key < CMB_000 + COMBO_KEYS_COUNT;
}

ComboKey combo_key_to_combo_key(uint16_t key) {
  if (combo_is_combo_key(key)) {
    return COMBO_KEY(key - CMB_000);
  } else {
    return NONE_COMBO_KEY;
  }
}

bool combo_has_key(Combo *combo, ComboKey key) {
  for (uint8_t i = 0; i < combo->size; ++i) {
    if (eq_combo_key(combo->array[i], key)) {
      return true;
    }
  }

  return false;
}

uint8_t combo_get_len(ComboPos elem_index) {
  for (uint8_t i = 0; i < COMBO_MAX_SIZE + 1; ++i) {
    if (eq_combo_key(COMBO_KEY(pgm_read_byte(&(combos[elem_index.repr].to_press[i]))), NONE_COMBO_KEY)) {
      return i;
    }
  }
  return 0;
}

bool combo_elem_has_key(ComboPos elem_index, ComboKey key) {
  uint8_t len = combo_get_len(elem_index);
  for (uint8_t i = 0; i < len; ++i) {
    if (eq_combo_key(COMBO_KEY(pgm_read_byte(&(combos[elem_index.repr].to_press[i]))), key)) {
      return true;
    }
  }

  return false;
}

ComboPos combo_get_pos(Combo *combo) {
  for (uint8_t i = 0; i < combos_size; ++i) {
    if (combo_get_len(COMBO_POS(i)) == combo->size) {
      bool found = true;
      for (uint8_t j = 0; j < combo->size; ++j) {
        found &= combo_has_key(combo, COMBO_KEY(pgm_read_byte(&(combos[i].to_press[j]))));
      }
      if (found) {
        return COMBO_POS(i);
      }
    }
  }
  return NONE_COMBO_POS;
}

bool combo_has_prefix(Combo *combo, ComboKey another_key) {
  for (uint8_t i = 0; i < combos_size; ++i) {
    ComboPos pos = COMBO_POS(i);
    if (combo_get_len(pos) > combo->size) {
      bool found = combo_elem_has_key(pos, another_key);
      for (uint8_t j = 0; j < combo->size; ++j) {
        found &= combo_elem_has_key(pos, combo->array[j]);
      }
      if (found) {
        return true;
      }
    }
  }
  return false;
}

uint16_t combo_get_keycode(ComboPos pos) {
  return pgm_read_word(&(combos[pos.repr].keycode));
}

uint16_t combo_get_undo(ComboPos elem_index) {
  return pgm_read_word(&(combos[elem_index.repr].undo_keycode));
}

bool combo_is_immediate(ComboPos elem_index) {
  return combo_get_undo(elem_index) != 0;
}

void combo_press(ComboPos pos, bool down) {
  #ifdef COMBO_DEBUG
  uprintf("combo press pos: %d %s\n", pos, down ? "down" : "up");
  #endif

  combo_enabled = false;
  press_arbitrary_keycode(combo_get_keycode(pos), down);
  combo_enabled = true;
}

void combo_press_undo(ComboPos pos) {
  #ifdef COMBO_DEBUG
  uprintf("combo press undo up: %d\n", pos);
  #endif

  combo_enabled = false;
  press_arbitrary_keycode(combo_get_undo(pos), false);
  combo_enabled = true;
}

void process_as_usual(keyrecord_t* record) {
  #ifdef COMBO_DEBUG
  uprintf("process as usual\n");
  #endif
  combo_enabled = false;
  process_record(record);
  combo_enabled = true;
}

void process_combo_as_usual(keyrecord_t* record) {
  #ifdef COMBO_DEBUG
  uprintf("process combo as usual\n");
  #endif
  combo_k_enabled = false;
  process_record(record);
  combo_k_enabled = true;
}

void combo_onenter_1(Combo *combo) {
  combo->state = 1;
}

void combo_onenter_2(Combo *combo, ComboPos pos, keyrecord_t* record) {
  combo_press(pos, true);
  process_as_usual(record);
  combo->state = 2;
}

void combo_onenter_end(Combo *combo) {
  uint8_t pos = 0;
  for (uint8_t i = 0; i < combo_stack_size; ++i) {
    if (&combo_stack[i] == combo) {
      pos = i;
    }
  }

  combo_stack_size--;

  for (uint8_t i = pos; i < combo_stack_size; ++i) {
    combo_stack[i] = combo_stack[i+1];
  }
}

void combo_onenter_3(Combo *combo, ComboKey key) {
  uint8_t pos = 0;
  for (uint8_t i = 0; i < combo->size; ++i) {
    if (eq_combo_key(combo->array[i], key)) {
      pos = i;
    }
  }

  combo->size--;

  for (uint8_t i = pos; i < combo->size; ++i) {
    combo->array[i] = combo->array[i+1];
  }

  combo->state = 3;

  if (combo->size == 0) {
    combo_onenter_end(combo);
  }
}

bool combo_process_1(Combo *combo, uint16_t key, keyrecord_t *record) {
  bool down = record->event.pressed;
  bool up = !down;
  ComboKey key_combo = combo_key_to_combo_key(key);
  ComboPos pos = combo_get_pos(combo);

  if (down && neq_combo_key(key_combo, NONE_COMBO_KEY)) {
    if (combo_has_prefix(combo, key_combo)) {
      if (combo->size == COMBO_MAX_SIZE) {
        combo_max_size_error();
      } else {
        combo->array[combo->size] = key_combo;
        combo->size++;
        combo->last_modify_time = timer_read();
        TRANSITION_DEBUG(e);

        ComboPos newpos = combo_get_pos(combo);
        if (combo_is_immediate(newpos)) {
          combo_press(newpos, true);
          combo->state = 4;
        }
      }
      return false;
    } else {
      if (neq_combo_pos(pos, NONE_COMBO_POS) && combo_k_enabled) {
        combo_press(pos, true);
        process_combo_as_usual(record);
        combo->state = 2;
        TRANSITION_DEBUG(k);
        return false;
      }
    }
  }

  if (neq_combo_pos(pos, NONE_COMBO_POS)) {
    if (neq_combo_key(key_combo, NONE_COMBO_KEY)) {
      if (up && combo_has_key(combo, key_combo)) {
        combo_press(pos, true);
        combo_press(pos, false);

        combo_onenter_3(combo, key_combo);
        TRANSITION_DEBUG(g);
        if (combo->size == 0) {
          TRANSITION_DEBUG(i);
        }
        return false;  
      }
    } else {
      if (down) {
        combo_onenter_2(combo, pos, record);
        TRANSITION_DEBUG(b);
        return false;
      }
    }
  } else {
    if (up && neq_combo_key(key_combo, NONE_COMBO_KEY)) {
      combo_onenter_3(combo, key_combo);
      TRANSITION_DEBUG(f);
      if (combo->size == 0) {
        TRANSITION_DEBUG(i);
      }
      return false;
    }
  }

  return true;
}

bool combo_process_2(Combo *combo, uint16_t key, keyrecord_t *record) {
  bool down = record->event.pressed;
  bool up = !down;
  ComboKey key_combo = combo_key_to_combo_key(key);

  if (up && neq_combo_key(key_combo, NONE_COMBO_KEY) && combo_has_key(combo, key_combo)) {

    ComboPos pos = combo_get_pos(combo);
    combo_press(pos, false);

    combo_onenter_3(combo, key_combo);
    TRANSITION_DEBUG(c);
    if (combo->size == 0) {
      TRANSITION_DEBUG(i);
    }
    return false;
  }

  return true;
}

bool combo_process_3(Combo *combo, uint16_t key, keyrecord_t *record) {
  bool down = record->event.pressed;
  bool up = !down;
  ComboKey key_combo = combo_key_to_combo_key(key);

  if (up && neq_combo_key(key_combo, NONE_COMBO_KEY) && combo_has_key(combo, key_combo)) {
    combo_onenter_3(combo, key_combo);
    TRANSITION_DEBUG(h);
    if (combo->size == 0) {
      TRANSITION_DEBUG(i);
    }
    return false;
  }

  return true;
}

bool combo_process_4(Combo *combo, uint16_t key, keyrecord_t *record) {
  bool down = record->event.pressed;
  bool up = !down;
  ComboKey key_combo = combo_key_to_combo_key(key);
  ComboPos pos = combo_get_pos(combo);

  if (down && neq_combo_key(key_combo, NONE_COMBO_KEY)) {
    if (combo_has_prefix(combo, key_combo)) {
      combo_press_undo(pos);
      combo->state = 1;
      TRANSITION_DEBUG(e4);
      return combo_process_1(combo, key, record);
    } else {
      if (neq_combo_pos(pos, NONE_COMBO_POS) && combo_k_enabled) {
        combo->state = 2;
        TRANSITION_DEBUG(k4);
        return true;
      }
    }
  }

  // This is guaranteed to be true
  if (!neq_combo_pos(pos, NONE_COMBO_POS)) {
    combo_max_count_error();
  }

  if (neq_combo_key(key_combo, NONE_COMBO_KEY)) {
    if (up && combo_has_key(combo, key_combo)) {
      TRANSITION_DEBUG(g4);
      return combo_process_1(combo, key, record);
    }
  } else {
    if (down) {
      TRANSITION_DEBUG(b4);
      return true;
    }
  }

  return true;
}


bool combo_process_local_states(Combo *combo, uint16_t key, keyrecord_t *record) {
  switch (combo->state) {
    case 1: return combo_process_1(combo, key, record);
    case 2: return combo_process_2(combo, key, record);
    case 3: return combo_process_3(combo, key, record);
    case 4: return combo_process_4(combo, key, record);
  }
  return true;
}

bool combo_process_record(uint16_t key, keyrecord_t *record) {
  if (!combo_enabled)
    return true;

  bool down = record->event.pressed;
  ComboKey key_combo = combo_key_to_combo_key(key);
  #ifdef COMBO_DEBUG
  uprintf("%d pressed %s\n", key_combo, down ? "down" : "up");
  #endif

  for (uint8_t i = 0; i < combo_stack_size; ++i) {
    Combo *combo = &combo_stack[i];
    if (!combo_process_local_states(combo, key, record))
      return false;
  }

  if (down && neq_combo_key(key_combo, NONE_COMBO_KEY)) {
    if (combo_stack_size == COMBO_STACK_MAX_SIZE) {
      combo_max_count_error();
    } else {
      Combo* combo = &combo_stack[combo_stack_size];
      combo_stack_size++;
      combo->array[0] = key_combo;
      combo->size = 1;
      combo->state = 1;
      combo->last_modify_time = timer_read();
      TRANSITION_DEBUG(a);

      ComboPos pos = combo_get_pos(combo);
      if (combo_is_immediate(pos)) {
        combo_press(pos, true);
        combo->state = 4;
      }
    }
    return false;
  }

  return true;
}

void combo_user_timer(void) {
  for (int i = 0; i < combo_stack_size; ++i) {
    Combo* combo = &combo_stack[i];
    if (combo->state == 1) {
      if (timer_read() - combo->last_modify_time > COMBO_WAIT_TIME) {
        ComboPos pos = combo_get_pos(combo);
        if (neq_combo_pos(pos, NONE_COMBO_POS)) {
          combo_press(pos, true);
          combo->state = 2;
          TRANSITION_DEBUG(d);
        }
      }
    } else if (combo->state == 4) {
      if (timer_read() - combo->last_modify_time > COMBO_WAIT_TIME) {
        ComboPos pos = combo_get_pos(combo);
        if (neq_combo_pos(pos, NONE_COMBO_POS)) {
          combo->state = 2;
          TRANSITION_DEBUG(d4);
        }
      }
    }
  }
}
