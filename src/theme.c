#include "theme.h"

#include <string.h>

#if defined(_WIN32)

#include <windows.h>

static System_Theme system_theme_get(void)
{
    HKEY key;
    const wchar_t *path =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return SYSTEM_THEME_LIGHT;
    }

    DWORD value = 1;
    DWORD type = 0;
    DWORD size = sizeof(value);
    LONG result = RegQueryValueExW(key, L"AppsUseLightTheme", NULL, &type,
                                   (BYTE *)&value, &size);
    RegCloseKey(key);

    if (result == ERROR_SUCCESS && type == REG_DWORD && value == 0) {
        return SYSTEM_THEME_DARK;
    }

    return SYSTEM_THEME_LIGHT;
}

#elif defined(__APPLE__)

#include <CoreFoundation/CoreFoundation.h>

static System_Theme system_theme_get(void)
{
    CFPropertyListRef value = CFPreferencesCopyAppValue(
        CFSTR("AppleInterfaceStyle"), CFSTR(".GlobalPreferences"));
    bool dark = value != NULL && CFGetTypeID(value) == CFStringGetTypeID() &&
                CFStringCompare((CFStringRef)value, CFSTR("Dark"),
                                kCFCompareCaseInsensitive) == kCFCompareEqualTo;

    if (value != NULL) CFRelease(value);
    return dark ? SYSTEM_THEME_DARK : SYSTEM_THEME_LIGHT;
}

#else

#include <gio/gio.h>

#define COLOR_SCHEME_KEY "color-scheme"
#define DESKTOP_INTERFACE_SCHEMA "org.gnome.desktop.interface"

static System_Theme gsettings_theme(GSettings *settings)
{
    gchar *value = g_settings_get_string(settings, COLOR_SCHEME_KEY);
    System_Theme theme = strcmp(value, "prefer-dark") == 0
                             ? SYSTEM_THEME_DARK
                             : SYSTEM_THEME_LIGHT;
    g_free(value);
    return theme;
}

static void color_scheme_changed(GSettings *settings, gchar *key,
                                 gpointer user_data)
{
    (void)key;
    System_Theme_Monitor *monitor = user_data;
    System_Theme theme = gsettings_theme(settings);

    if (theme != monitor->current) {
        monitor->current = theme;
        monitor->changed = true;
    }
}

#endif

void system_theme_monitor_init(System_Theme_Monitor *monitor)
{
    *monitor = (System_Theme_Monitor){
        .current = SYSTEM_THEME_LIGHT,
    };

#if defined(_WIN32) || defined(__APPLE__)
    monitor->current = system_theme_get();
#else
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();

    if (source == NULL) return;

    GSettingsSchema *schema = g_settings_schema_source_lookup(
        source, DESKTOP_INTERFACE_SCHEMA, true);

    if (schema == NULL) return;

    if (!g_settings_schema_has_key(schema, COLOR_SCHEME_KEY)) {
        g_settings_schema_unref(schema);
        return;
    }

    GSettings *settings = g_settings_new_full(schema, NULL, NULL);
    g_settings_schema_unref(schema);

    monitor->settings = settings;
    monitor->current = gsettings_theme(settings);
    g_signal_connect(settings, "changed::" COLOR_SCHEME_KEY,
                     G_CALLBACK(color_scheme_changed), monitor);
#endif
}

void system_theme_monitor_uninit(System_Theme_Monitor *monitor)
{
#if !defined(_WIN32) && !defined(__APPLE__)
    if (monitor->settings != NULL) g_object_unref(monitor->settings);
#endif
    *monitor = (System_Theme_Monitor){0};
}

System_Theme system_theme_monitor_get(const System_Theme_Monitor *monitor)
{
    return monitor->current;
}

bool system_theme_monitor_update(System_Theme_Monitor *monitor, double now)
{
#if defined(_WIN32) || defined(__APPLE__)
    if (now >= monitor->next_poll) {
        monitor->next_poll = now + 1.0;
        System_Theme theme = system_theme_get();

        if (theme != monitor->current) {
            monitor->current = theme;
            monitor->changed = true;
        }
    }
#else
    (void)now;

    while (g_main_context_iteration(NULL, false)) {
    }
#endif

    bool changed = monitor->changed;
    monitor->changed = false;
    return changed;
}
