#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <pty.h>
#include <errno.h>
#include <sys/select.h>

// GTK para la interfaz gráfica
#include <gtk/gtk.h>
#include <vte/vte.h>

#define BUFFER_SIZE 4096
#define CLIPBOARD_SIZE 8192

// Variables globales
static GtkWidget *window;
static GtkWidget *terminal;
static char clipboard_internal[CLIPBOARD_SIZE] = {0};

// Configuración visual
#define BG_COLOR "#0f172a"      // Azul muy oscuro (slate-900)
#define FG_COLOR "#94a3b8"      // Azul grisáceo claro
#define FONT_FAMILY "monospace"
#define FONT_SIZE 12

void setup_terminal_appearance(VteTerminal *vte_terminal) {
    // Configurar colores
    GdkRGBA bg_color, fg_color;
    
    gdk_rgba_parse(&bg_color, BG_COLOR);
    gdk_rgba_parse(&fg_color, FG_COLOR);
    
    // Aplicar colores de fondo y texto
    vte_terminal_set_color_background(vte_terminal, &bg_color);
    vte_terminal_set_color_foreground(vte_terminal, &fg_color);
    
    // Configurar fuente
    PangoFontDescription *font_desc = pango_font_description_new();
    pango_font_description_set_family(font_desc, FONT_FAMILY);
    pango_font_description_set_size(font_desc, FONT_SIZE * PANGO_SCALE);
    vte_terminal_set_font(vte_terminal, font_desc);
    pango_font_description_free(font_desc);
    
    // Configuraciones adicionales
    vte_terminal_set_cursor_blink_mode(vte_terminal, VTE_CURSOR_BLINK_ON);
    vte_terminal_set_scrollback_lines(vte_terminal, 10000);
    vte_terminal_set_allow_bold(vte_terminal, TRUE);
}

int detect_display_server(void) {
    if (getenv("WAYLAND_DISPLAY")) {
        return 1; // Wayland
    } else if (getenv("DISPLAY")) {
        return 0; // X11
    }
    return -1; // Desconocido
}

void copy_to_system_clipboard(const char* text) {
    int display_type = detect_display_server();
    FILE* pipe = NULL;
    
    if (display_type == 1) {
        // Wayland
        pipe = popen("wl-copy", "w");
    } else if (display_type == 0) {
        // X11
        pipe = popen("xclip -selection clipboard", "w");
        if (pipe == NULL) {
            pipe = popen("xsel --clipboard --input", "w");
        }
    }
    
    if (pipe != NULL) {
        fprintf(pipe, "%s", text);
        pclose(pipe);
    } else {
        // Fallback al clipboard interno
        strncpy(clipboard_internal, text, CLIPBOARD_SIZE - 1);
        clipboard_internal[CLIPBOARD_SIZE - 1] = '\0';
    }
}

char* paste_from_system_clipboard(void) {
    static char paste_buffer[CLIPBOARD_SIZE];
    int display_type = detect_display_server();
    FILE* pipe = NULL;
    
    if (display_type == 1) {
        // Wayland
        pipe = popen("wl-paste", "r");
    } else if (display_type == 0) {
        // X11
        pipe = popen("xclip -selection clipboard -o", "r");
        if (pipe == NULL) {
            pipe = popen("xsel --clipboard --output", "r");
        }
    }
    
    if (pipe != NULL) {
        size_t len = fread(paste_buffer, 1, CLIPBOARD_SIZE - 1, pipe);
        paste_buffer[len] = '\0';
        pclose(pipe);
        
        if (len > 0) {
            return paste_buffer;
        }
    }
    
    // Fallback al clipboard interno
    if (strlen(clipboard_internal) > 0) {
        strcpy(paste_buffer, clipboard_internal);
        return paste_buffer;
    }
    
    return NULL;
}

gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    VteTerminal *vte_terminal = VTE_TERMINAL(user_data);
    
    // Ctrl+Shift+C para copiar
    if ((event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == 
        (GDK_CONTROL_MASK | GDK_SHIFT_MASK) && event->keyval == GDK_KEY_C) {
        
        if (vte_terminal_get_has_selection(vte_terminal)) {
            vte_terminal_copy_clipboard_format(vte_terminal, VTE_FORMAT_TEXT);
            
            // También copiar con herramientas del sistema
            char *selection = vte_terminal_get_text_selected(vte_terminal, VTE_FORMAT_TEXT);
            if (selection) {
                copy_to_system_clipboard(selection);
                g_free(selection);
            }
        }
        return TRUE;
    }
    
    // Ctrl+Shift+V para pegar
    if ((event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == 
        (GDK_CONTROL_MASK | GDK_SHIFT_MASK) && event->keyval == GDK_KEY_V) {
        
        // Primero intentar pegar desde el clipboard de GTK
        vte_terminal_paste_clipboard(vte_terminal);
        
        // Si no hay nada, intentar con herramientas del sistema
        char *clipboard_text = paste_from_system_clipboard();
        if (clipboard_text && strlen(clipboard_text) > 0) {
            vte_terminal_feed_child(vte_terminal, clipboard_text, strlen(clipboard_text));
        }
        
        return TRUE;
    }
    
    // Ctrl+Shift+Q para salir
    if ((event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == 
        (GDK_CONTROL_MASK | GDK_SHIFT_MASK) && event->keyval == GDK_KEY_Q) {
        gtk_main_quit();
        return TRUE;
    }
    
    return FALSE;
}

void on_child_exited(VteTerminal *terminal, gint status, gpointer user_data) {
    (void)terminal;
    (void)status;
    (void)user_data;
    gtk_main_quit();
}

void on_window_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    (void)user_data;
    gtk_main_quit();
}

void show_help(void) {
    printf("Water Terminal - Terminal gráfica minimalista\n");
    printf("Uso: water [opciones]\n\n");
    printf("Opciones:\n");
    printf("  -c \"comando\"    Ejecutar comando al iniciar\n");
    printf("  -h, --help      Mostrar esta ayuda\n\n");
    printf("Atajos de teclado:\n");
    printf("  Ctrl+Shift+C    Copiar texto seleccionado\n");
    printf("  Ctrl+Shift+V    Pegar texto\n");
    printf("  Ctrl+Shift+Q    Salir de la terminal\n\n");
    printf("Características:\n");
    printf("  - Fondo azul oscuro con texto azul claro\n");
    printf("  - Soporte para Wayland y X11\n");
    printf("  - Basado en VTE (Virtual Terminal Emulator)\n");
}

int main(int argc, char *argv[]) {
    char *initial_command = NULL;
    
    // Parsear argumentos antes de inicializar GTK
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            initial_command = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            show_help();
            return 0;
        }
    }
    
    // Inicializar GTK
    gtk_init(&argc, &argv);
    
    // Crear ventana principal
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Water Terminal");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    
    // Crear el widget de terminal VTE
    terminal = vte_terminal_new();
    
    // Configurar la apariencia de la terminal
    setup_terminal_appearance(VTE_TERMINAL(terminal));
    
    // Configurar eventos
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), terminal);
    g_signal_connect(terminal, "child-exited", G_CALLBACK(on_child_exited), NULL);
    
    // Hacer que la ventana pueda recibir eventos de teclado
    gtk_widget_set_can_focus(window, TRUE);
    
    // Agregar el terminal a la ventana
    gtk_container_add(GTK_CONTAINER(window), terminal);
    
    // Iniciar el shell o comando
    char **command_argv = NULL;
    if (initial_command) {
        command_argv = g_strsplit(initial_command, " ", 0);
    } else {
        // Shell por defecto
        char *shell = getenv("SHELL");
        if (shell == NULL) {
            shell = "/bin/bash";
        }
        command_argv = g_new(char*, 2);
        command_argv[0] = g_strdup(shell);
        command_argv[1] = NULL;
    }
    
    // Spawnar el proceso
    vte_terminal_spawn_sync(VTE_TERMINAL(terminal),
                           VTE_PTY_DEFAULT,
                           NULL, // directorio de trabajo (NULL = actual)
                           command_argv,
                           NULL, // environment (NULL = heredar)
                           G_SPAWN_SEARCH_PATH,
                           NULL, NULL, // setup function
                           NULL, // pid (no necesario)
                           NULL, // cancellable
                           NULL); // error
    
    // Limpiar
    g_strfreev(command_argv);
    
    // Mostrar la ventana
    gtk_widget_show_all(window);
    
    // Dar foco al terminal
    gtk_widget_grab_focus(terminal);
    
    // Ejecutar el loop principal de GTK
    gtk_main();
    
    return 0;
}
