/*
 * Copyright (c) 2026 Luke Canada
 * SPDX-License-Identifier: MIT
 */

/* Persistence checks for settings defaults and row vectorization toggles. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/settings.h"

static void test_settings_round_trip(void)
{
    char path[] = "/tmp/ttb2_settings_testXXXXXX";
    int fd = mkstemp(path);
    AppSettings settings;
    AppSettings loaded;

    assert(fd >= 0);
    close(fd);
    unlink(path);

    settings_init_defaults(&settings);
    assert(settings.row_vectorization_enabled == true);

    settings.autosave_enabled = false;
    settings.type_infer_enabled = false;
    settings.show_row_gutter = false;
    settings.row_vectorization_enabled = false;
    settings.theme_id = 2;

    assert(settings_save(path, &settings) == 0);

    settings_init_defaults(&loaded);
    assert(settings_load(path, &loaded) == 0);
    assert(loaded.autosave_enabled == false);
    assert(loaded.type_infer_enabled == false);
    assert(loaded.show_row_gutter == false);
    assert(loaded.row_vectorization_enabled == false);
    assert(loaded.theme_id == 2);
    assert(settings_row_vectorization_enabled() == false);

    unlink(path);
}

int main(void)
{
    test_settings_round_trip();
    puts("settings tests passed");
    return 0;
}
