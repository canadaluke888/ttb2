#include "settings.h"
#include <json-c/json.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define SETTINGS_DIR "settings"
#define SETTINGS_FILE SETTINGS_DIR "/settings.json"

const char *settings_default_path(void)
{
    return SETTINGS_FILE;
}

int settings_ensure_directory(void)
{
    struct stat st;
    if (stat(SETTINGS_DIR, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        return -1;
    }
    if (mkdir(SETTINGS_DIR, 0755) == 0) {
        return 0;
    }
    if (errno == EEXIST) {
        return 0;
    }
    return -1;
}

void settings_init_defaults(AppSettings *s) {
    if (!s) return;
    s->autosave_enabled = true;
    s->type_infer_enabled = true;
    s->low_ram_enabled = false;
    s->show_row_gutter = true;
}

int settings_load(const char *path, AppSettings *out) {
    if (!out) return -1;
    settings_init_defaults(out);
    if (!path || !path[0]) path = settings_default_path();

    struct json_object *root = json_object_from_file(path);
    if (!root) return -1;
    struct json_object *jauto = NULL;
    if (json_object_object_get_ex(root, "autosave_enabled", &jauto)) {
        out->autosave_enabled = json_object_get_boolean(jauto);
    }
    struct json_object *jinf = NULL;
    if (json_object_object_get_ex(root, "type_infer_enabled", &jinf)) {
        out->type_infer_enabled = json_object_get_boolean(jinf);
    }
    struct json_object *jlow = NULL;
    if (json_object_object_get_ex(root, "low_ram_enabled", &jlow)) {
        out->low_ram_enabled = json_object_get_boolean(jlow);
    }
    struct json_object *jg = NULL;
    if (json_object_object_get_ex(root, "show_row_gutter", &jg)) {
        out->show_row_gutter = json_object_get_boolean(jg);
    }
    json_object_put(root);
    return 0;
}

int settings_save(const char *path, const AppSettings *s) {
    if (!s) return -1;
    if (!path || !path[0]) path = settings_default_path();
    if (settings_ensure_directory() != 0) {
        return -1;
    }
    struct json_object *root = json_object_new_object();
    json_object_object_add(root, "autosave_enabled", json_object_new_boolean(s->autosave_enabled));
    json_object_object_add(root, "type_infer_enabled", json_object_new_boolean(s->type_infer_enabled));
    json_object_object_add(root, "low_ram_enabled", json_object_new_boolean(s->low_ram_enabled));
    json_object_object_add(root, "show_row_gutter", json_object_new_boolean(s->show_row_gutter));
    int rc = json_object_to_file_ext(path, root, JSON_C_TO_STRING_PRETTY);
    json_object_put(root);
    return (rc == 0) ? 0 : -1;
}
