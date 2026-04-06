// Copyright 2025 Ruben Berenguel

#include "keymap.h"
#include <cstddef>

bool g_use_qwerty = false;  // Default: Colemak

char get_char_from_key(const uint8_t key, KeyModifiers* modifiers) {
    // Colemak — tested
    static const char colemak[] = {
        '\0', '\0', '\0', '\0', 'a',  'b',  'c',  's',  'f',  't', 'd', 'h',
        'u',  'n',  'e',  'i',  'm',  'k',  'y',  ';',  'q',  'p', 'r', 'g',
        'l',  'v',  'w',  'x',  'j',  'z',  '1',  '2',  '3',  '4', '5', '6',
        '7',  '8',  '9',  '0',  '\0', '\0', '\0', '\0', '\0', '-', '=', '[',
        ']',  '|',  '#',  'o',  '\'', '`',  ',',  '.',  '/'};
    static const char colemak_shift[] = {
        '\0', '\0', '\0', '\0', 'A',  'B',  'C',  'S',  'F',  'T', 'D', 'H',
        'U',  'N',  'E',  'I',  'M',  'K',  'Y',  ':',  'Q',  'P', 'R', 'G',
        'L',  'V',  'W',  'X',  'J',  'Z',  '!',  '@',  '#',  '$', '%', '^',
        '&',  '*',  '(',  ')',  '\0', '\0', '\0', '\0', '\0', '_', '+', '{',
        '}',  '\\', '#',  'O',  '"',  '~',  '<',  '>',  '?'};

    // QWERTY
    static const char qwerty[] = {
        '\0', '\0', '\0', '\0', 'a',  'b',  'c',  'd',  'e',  'f', 'g', 'h',
        'i',  'j',  'k',  'l',  'm',  'n',  'o',  'p',  'q',  'r', 's', 't',
        'u',  'v',  'w',  'x',  'y',  'z',  '1',  '2',  '3',  '4', '5', '6',
        '7',  '8',  '9',  '0',  '\0', '\0', '\0', '\0', '\0', '-', '=', '[',
        ']',  '|',  '#',  ';',  '\'', '`',  ',',  '.',  '/'};
    static const char qwerty_shift[] = {
        '\0', '\0', '\0', '\0', 'A',  'B',  'C',  'D',  'E',  'F', 'G', 'H',
        'I',  'J',  'K',  'L',  'M',  'N',  'O',  'P',  'Q',  'R', 'S', 'T',
        'U',  'V',  'W',  'X',  'Y',  'Z',  '!',  '@',  '#',  '$', '%', '^',
        '&',  '*',  '(',  ')',  '\0', '\0', '\0', '\0', '\0', '_', '+', '{',
        '}',  '\\', '#',  ':',  '"',  '~',  '<',  '>',  '?'};

    const char* map       = g_use_qwerty ? qwerty       : colemak;
    const char* shift_map = g_use_qwerty ? qwerty_shift : colemak_shift;
    const size_t map_size = g_use_qwerty ? sizeof(qwerty) : sizeof(colemak);

    if (key >= map_size) {
        return '\0';
    }

    return modifiers->shift ? shift_map[key] : map[key];
}
