#include "workspace.h"
#include "ttb_io.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>

#define WORKSPACE_DIR "workspace"
#define WORKSPACE_DEFAULT_FILE WORKSPACE_DIR "/session.ttbx"

static char g_project_path[PATH_MAX] = WORKSPACE_DEFAULT_FILE;
static int g_autosave_on = 1;
static Table *g_active_table = NULL;

static void set_err(char *err, size_t err_sz, const char *msg)
{
    if (!err || err_sz == 0 || !msg) {
        return;
    }
    strncpy(err, msg, err_sz - 1);
    err[err_sz - 1] = '\0';
}

static int ensure_workspace_dir(char *err, size_t err_sz)
{
    struct stat st;
    if (stat(WORKSPACE_DIR, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        set_err(err, err_sz, "workspace path is not a directory");
        return -1;
    }
    if (mkdir(WORKSPACE_DIR, 0755) != 0) {
        if (errno == EEXIST) return 0;
        set_err(err, err_sz, "failed to create workspace directory");
        return -1;
    }
    return 0;
}

static void copy_project_path(const char *path)
{
    if (!path || !*path) {
        strncpy(g_project_path, WORKSPACE_DEFAULT_FILE, sizeof(g_project_path) - 1);
        g_project_path[sizeof(g_project_path) - 1] = '\0';
        return;
    }
    strncpy(g_project_path, path, sizeof(g_project_path) - 1);
    g_project_path[sizeof(g_project_path) - 1] = '\0';
}

int workspace_set_project_path(const char *path)
{
    if (!path || !*path) {
        copy_project_path(WORKSPACE_DEFAULT_FILE);
        return 0;
    }
    copy_project_path(path);
    return 0;
}

const char *workspace_project_path(void)
{
    return g_project_path;
}

void workspace_set_autosave_enabled(int enabled)
{
    g_autosave_on = enabled ? 1 : 0;
}

int workspace_autosave_enabled(void)
{
    return g_autosave_on;
}

void workspace_set_active_table(Table *table)
{
    g_active_table = table;
}

static int save_project(const Table *table, const char *path, char *err, size_t err_sz)
{
    if (!table) {
        set_err(err, err_sz, "No table to save");
        return -1;
    }
    if (ensure_workspace_dir(err, err_sz) != 0) {
        return -1;
    }
    return ttbx_save(table, path, err, err_sz);
}

int workspace_autosave(const Table *table, char *err, size_t err_sz)
{
    if (!g_autosave_on) {
        return 0;
    }
    workspace_set_active_table((Table *)table);
    return save_project(table, g_project_path, err, err_sz);
}

int workspace_manual_save(const Table *table, char *err, size_t err_sz)
{
    workspace_set_active_table((Table *)table);
    return save_project(table, g_project_path, err, err_sz);
}

int workspace_init(Table **out_table, char *err, size_t err_sz)
{
    if (ensure_workspace_dir(err, err_sz) != 0) {
        return -1;
    }

    copy_project_path(WORKSPACE_DEFAULT_FILE);

    Table *table = NULL;
    if (access(g_project_path, F_OK) == 0) {
        table = ttbx_load(g_project_path, err, err_sz);
    }

    if (!table) {
        table = create_table("Untitled Table");
        if (!table) {
            set_err(err, err_sz, "Failed to allocate table");
            return -1;
        }
        workspace_set_active_table(table);
        workspace_autosave(table, NULL, 0);
    } else {
        workspace_set_active_table(table);
    }

    if (out_table) {
        *out_table = table;
    }
    return 0;
}
