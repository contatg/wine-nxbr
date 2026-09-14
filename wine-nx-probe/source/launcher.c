/*
 * Launcher do Wine-NX: menu dos programas Windows encontrados em drive_c,
 * controlado pelo controle do Nintendo Switch.
 *
 * Perfis Box64 por jogo:
 *   ZL = abrir configuracao do jogo
 *   A  = confirmar
 *   B  = voltar
 *   +  = sair
 */

#include <switch.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "launcher_list.h"

#define LAUNCHER_ROWS  36
#define LAUNCHER_DEPTH 3

#define LAUNCHER_REPEAT_DELAY_NS 400000000ull
#define LAUNCHER_REPEAT_NS       70000000ull

typedef int (*launcher_machine_fn)( const char *path, unsigned short *machine );

static struct launcher_entry launcher_entries[LAUNCHER_MAX_ENTRIES];


/* ------------------------------------------------------------------------- */
/* Procura os programas                                                       */
/* ------------------------------------------------------------------------- */

static void launcher_scan( const char *dir, const char *dos_dir, int depth, int *count,
                           launcher_machine_fn machine_of )
{
    struct dirent *entry;
    DIR *handle;

    if (!(handle = opendir( dir ))) return;

    while (*count < LAUNCHER_MAX_ENTRIES && (entry = readdir( handle )))
    {
        struct launcher_entry *item = &launcher_entries[*count];
        char path[sizeof(item->path)], dos[sizeof(item->dos)];
        struct stat st;
        int is_dir;

        if (entry->d_name[0] == '.') continue;

        if ((size_t)snprintf( path, sizeof(path), "%s/%s",
                              dir, entry->d_name ) >= sizeof(path))
            continue;

        if ((size_t)snprintf( dos, sizeof(dos), "%s\\%s",
                              dos_dir, entry->d_name ) >= sizeof(dos))
            continue;

        if (entry->d_type == DT_DIR)
            is_dir = 1;
        else if (entry->d_type == DT_REG)
            is_dir = 0;
        else if (stat( path, &st ))
            continue;
        else
            is_dir = S_ISDIR( st.st_mode );

        if (is_dir)
        {
            /* Os arquivos internos do Wine ficam em windows. */
            if (!depth && !strcasecmp( entry->d_name, "windows" ))
                continue;

            if (depth + 1 < LAUNCHER_DEPTH)
                launcher_scan( path, dos, depth + 1, count, machine_of );
        }
        else if (launcher_is_exe( entry->d_name ) &&
                 !machine_of( path, &item->machine ))
        {
            memcpy( item->path, path, sizeof(path) );
            memcpy( item->dos, dos, sizeof(dos) );
            (*count)++;
        }
    }

    closedir( handle );
}


/* ------------------------------------------------------------------------- */
/* Arquivos do runtime                                                        */
/* ------------------------------------------------------------------------- */

static void launcher_write_line( const char *runtime_dir,
                                  const char *name,
                                  const char *text )
{
    char path[256];
    FILE *file;

    snprintf( path, sizeof(path), "%s/%s", runtime_dir, name );

    if (!(file = fopen( path, "w" )))
        return;

    fprintf( file, "%s\n", text );
    fclose( file );
}


/* ------------------------------------------------------------------------- */
/* Configuracao Box64 por jogo                                                */
/* ------------------------------------------------------------------------- */

static int launcher_box64_path( const char *exe_path,
                                char *out,
                                size_t size )
{
    return launcher_sibling_path( exe_path, ".box64.txt", out, size );
}


/*
 * Perfil padrao:
 * remove o arquivo especifico do jogo.
 */
static void launcher_box64_default( const char *exe_path )
{
    char path[512];

    if (!launcher_box64_path( exe_path, path, sizeof(path) ))
        return;

    remove( path );
}


/*
 * Perfil de desempenho.
 */
static void launcher_box64_performance( const char *exe_path )
{
    char path[512];
    FILE *file;

    if (!launcher_box64_path( exe_path, path, sizeof(path) ))
        return;

    file = fopen( path, "w" );
    if (!file)
        return;

    fprintf( file, "# Perfil de desempenho Wine-NX\n" );
    fprintf( file, "BOX64_DYNAREC_BIGBLOCK=3\n" );
    fprintf( file, "BOX64_DYNAREC_CALLRET=2\n" );
    fprintf( file, "BOX64_DYNAREC_FASTNAN=1\n" );
    fprintf( file, "BOX64_DYNAREC_FASTROUND=1\n" );
    fprintf( file, "BOX64_DYNAREC_NATIVEFLAGS=1\n" );
    fprintf( file, "BOX64_DYNAREC_FORWARD=128\n" );

    fclose( file );
}


/*
 * Perfil de compatibilidade.
 */
static void launcher_box64_compatibility( const char *exe_path )
{
    char path[512];
    FILE *file;

    if (!launcher_box64_path( exe_path, path, sizeof(path) ))
        return;

    file = fopen( path, "w" );
    if (!file)
        return;

    fprintf( file, "# Perfil de compatibilidade Wine-NX\n" );
    fprintf( file, "BOX64_DYNAREC_BIGBLOCK=1\n" );
    fprintf( file, "BOX64_DYNAREC_CALLRET=2\n" );
    fprintf( file, "BOX64_DYNAREC_FASTNAN=0\n" );
    fprintf( file, "BOX64_DYNAREC_FASTROUND=0\n" );
    fprintf( file, "BOX64_DYNAREC_NATIVEFLAGS=0\n" );
    fprintf( file, "BOX64_DYNAREC_FORWARD=32\n" );

    fclose( file );
}


/*
 * Descobre qual perfil esta atualmente configurado.
 *
 * 0 = Padrao
 * 1 = Desempenho
 * 2 = Compatibilidade
 * 3 = Personalizado
 */
static int launcher_box64_profile( const char *exe_path )
{
    char path[512];
    char line[256];
    FILE *file;

    if (!launcher_box64_path( exe_path, path, sizeof(path) ))
        return 0;

    file = fopen( path, "r" );
    if (!file)
        return 0;

    while (fgets( line, sizeof(line), file ))
    {
        if (strstr( line, "BOX64_DYNAREC_BIGBLOCK=3" ))
        {
            fclose( file );
            return 1;
        }

        if (strstr( line, "BOX64_DYNAREC_BIGBLOCK=1" ))
        {
            fclose( file );
            return 2;
        }
    }

    fclose( file );
    return 3;
}


static const char *launcher_profile_name( int profile )
{
    switch (profile)
    {
        case 0: return "Padrao";
        case 1: return "Desempenho";
        case 2: return "Compatibilidade";
        default: return "Personalizado";
    }
}


/* ------------------------------------------------------------------------- */
/* Menu de configuracao do jogo                                              */
/* ------------------------------------------------------------------------- */

static int launcher_profile_menu( const char *exe_path,
                                  const char *dos_path )
{
    PadState pad;
    int selected = launcher_box64_profile( exe_path );
    int redraw = 1;

    /*
     * So existem tres opcoes selecionaveis.
     * Se ja houver um arquivo personalizado, comeca no Padrao.
     */
    if (selected > 2)
        selected = 0;

    padConfigureInput( 1, HidNpadStyleSet_NpadStandard );
    padInitializeDefault( &pad );

    while (appletMainLoop())
    {
        u64 down;

        padUpdate( &pad );
        down = padGetButtonsDown( &pad );

        /*
         * + = sair do Wine-NX.
         */
        if (down & HidNpadButton_Plus)
            return -1;

        /*
         * B = voltar para a lista.
         */
        if (down & HidNpadButton_B)
            return 0;

        if (down & HidNpadButton_AnyUp)
        {
            selected--;

            if (selected < 0)
                selected = 2;

            redraw = 1;
        }

        if (down & HidNpadButton_AnyDown)
        {
            selected++;

            if (selected > 2)
                selected = 0;

            redraw = 1;
        }

        /*
         * A = confirmar perfil.
         */
        if (down & HidNpadButton_A)
        {
            switch (selected)
            {
                case 0:
                    launcher_box64_default( exe_path );
                    break;

                case 1:
                    launcher_box64_performance( exe_path );
                    break;

                case 2:
                    launcher_box64_compatibility( exe_path );
                    break;
            }

            return 1;
        }

        if (redraw)
        {
            int i;

            printf( CONSOLE_ESC(2J) CONSOLE_ESC(1;1H) );

            printf( CONSOLE_CYAN
                    "Wine-NX"
                    CONSOLE_RESET
                    "  Configuracao do jogo\n\n" );

            printf( "Jogo: %s\n\n", dos_path );

            for (i = 0; i < 3; i++)
            {
                if (i == selected)
                    printf( CONSOLE_GREEN
                            " > %s"
                            CONSOLE_RESET,
                            launcher_profile_name(i) );
                else
                    printf( "   %s",
                            launcher_profile_name(i) );

                printf( "\n" );
            }

            printf( "\nPerfil atual: %s\n",
                    launcher_profile_name(
                        launcher_box64_profile( exe_path ) ) );

            printf( CONSOLE_ESC(44;1H)
                    "A confirmar  "
                    "Cima/Baixo escolher  "
                    "B voltar  "
                    "+ sair" );

            consoleUpdate( NULL );

            redraw = 0;
        }

        svcSleepThread( 16000000 );
    }

    return -1;
}


/* ------------------------------------------------------------------------- */
/* Tela principal                                                             */
/* ------------------------------------------------------------------------- */

static void launcher_draw( const char *build,
                            int count,
                            int selected,
                            int first,
                            int verbose,
                            int profile,
                            const char *args )
{
    int i;

    printf( CONSOLE_ESC(2J) CONSOLE_ESC(1;1H) );

    printf( CONSOLE_CYAN
            "Wine-NX"
            CONSOLE_RESET
            "  %s\n",
            build );

    printf( "Escolha um programa Windows em:\n"
            "sdmc:/switch/wine/drive_c\n\n" );

    if (!count)
    {
        printf( CONSOLE_YELLOW
                "Nenhum programa Windows (.exe) encontrado.\n"
                CONSOLE_RESET );

        printf( "Copie os programas para "
                "sdmc:/switch/wine/drive_c "
                "e inicie o Wine-NX novamente.\n" );
    }

    for (i = first;
         i < count && i < first + LAUNCHER_ROWS;
         i++)
    {
        const char *arch =
            launcher_entries[i].machine == 0x014c
                ? "x86"
                : "ARM64";

        if (i == selected)
        {
            printf( CONSOLE_GREEN
                    " > %-68.68s %5s\n"
                    CONSOLE_RESET,
                    launcher_entries[i].dos,
                    arch );
        }
        else
        {
            printf( "   %-68.68s %5s\n",
                    launcher_entries[i].dos,
                    arch );
        }
    }

    printf( CONSOLE_ESC(41;1H) );

    if (count)
    {
        printf( "%d de %d",
                selected + 1,
                count );

        if (args)
            printf( "   argumentos: %.52s",
                    args );

        printf( "\n" );
    }

    printf( CONSOLE_ESC(44;1H)
            "A iniciar  "
            "Cima/Baixo escolher  "
            "L/R pagina  "
            "Y verbose: %s  "
            "X profiler: %s  "
            "ZL configuracoes  "
            "+ sair",
            verbose
                ? CONSOLE_YELLOW "ligado " CONSOLE_RESET
                : "desligado",
            profile
                ? CONSOLE_YELLOW "ligado " CONSOLE_RESET
                : "desligado" );

    consoleUpdate( NULL );
}


/* ------------------------------------------------------------------------- */
/* Launcher principal                                                        */
/* ------------------------------------------------------------------------- */

int wine_nx_launcher_run( const char *drive_c,
                          const char *runtime_dir,
                          const char *build,
                          launcher_machine_fn machine_of,
                          int *verbose,
                          int *profile,
                          char *target,
                          size_t target_size )
{
    const u64 moves =
        HidNpadButton_AnyUp |
        HidNpadButton_AnyDown |
        HidNpadButton_L |
        HidNpadButton_R;

    char args[896], args_path[256];

    int count = 0;
    int selected;
    int first = 0;
    int redraw = 1;
    int have_args;

    u64 repeat_at = 0;

    PadState pad;

    launcher_scan( drive_c,
                   "C:",
                   0,
                   &count,
                   machine_of );

    qsort( launcher_entries,
           count,
           sizeof(launcher_entries[0]),
           launcher_compare );

    selected = launcher_find( launcher_entries,
                              count,
                              target );

    snprintf( args_path,
              sizeof(args_path),
              "%s/args.txt",
              runtime_dir );

    {
        FILE *file = fopen( args_path, "r" );

        have_args = file &&
                    fgets( args,
                           sizeof(args),
                           file );

        if (file)
            fclose( file );

        if (have_args)
            args[strcspn( args, "\r\n" )] = 0;
    }

    padConfigureInput( 1, HidNpadStyleSet_NpadStandard );
    padInitializeDefault( &pad );

    while (appletMainLoop())
    {
        u64 now;
        u64 down;
        u64 held;
        u64 step = 0;

        now = armGetSystemTick();

        padUpdate( &pad );

        down = padGetButtonsDown( &pad );
        held = padGetButtons( &pad );

        /*
         * + = sair.
         */
        if (down & HidNpadButton_Plus)
            break;

        /*
         * Navegacao com repeticao.
         */
        if (down & moves)
        {
            step = down;

            repeat_at =
                now + armNsToTicks(
                    LAUNCHER_REPEAT_DELAY_NS );
        }
        else if ((held & moves) && now >= repeat_at)
        {
            step = held;

            repeat_at =
                now + armNsToTicks(
                    LAUNCHER_REPEAT_NS );
        }

        /*
         * Escolher jogo.
         */
        if (count && step)
        {
            int old = selected;

            if (step & HidNpadButton_AnyUp)
                selected--;

            if (step & HidNpadButton_AnyDown)
                selected++;

            if (step & HidNpadButton_L)
                selected -= LAUNCHER_ROWS;

            if (step & HidNpadButton_R)
                selected += LAUNCHER_ROWS;

            if (selected < 0)
                selected = 0;

            if (selected >= count)
                selected = count - 1;

            if (selected != old)
                redraw = 1;
        }

        /*
         * Y = verbose.
         */
        if (down & HidNpadButton_Y)
        {
            *verbose = !*verbose;

            launcher_write_line(
                runtime_dir,
                "verbose.txt",
                *verbose ? "1" : "0" );

            redraw = 1;
        }

        /*
         * X = profiler.
         */
        if (down & HidNpadButton_X)
        {
            *profile = !*profile;

            launcher_write_line(
                runtime_dir,
                "profile.txt",
                *profile ? "1" : "0" );

            redraw = 1;
        }

        /*
         * ZL = configuracoes do jogo.
         */
        if ((down & HidNpadButton_ZL) && count)
        {
            int result;

            result = launcher_profile_menu(
                launcher_entries[selected].path,
                launcher_entries[selected].dos );

            /*
             * + dentro do menu tambem sai do Wine-NX.
             */
            if (result < 0)
            {
                printf( CONSOLE_ESC(2J)
                        CONSOLE_ESC(1;1H) );

                consoleUpdate( NULL );

                return 0;
            }

            redraw = 1;
        }

        /*
         * A = iniciar o jogo.
         */
        if ((down & HidNpadButton_A) && count)
        {
            snprintf( target,
                      target_size,
                      "%s",
                      launcher_entries[selected].path );

            launcher_write_line(
                runtime_dir,
                "target.txt",
                target );

            printf( CONSOLE_ESC(2J)
                    CONSOLE_ESC(1;1H) );

            consoleUpdate( NULL );

            return 1;
        }

        /*
         * Atualiza a tela.
         */
        if (redraw)
        {
            const char *shown = NULL;

            char own_path[
                sizeof(launcher_entries[0].path)
            ];

            char own[256];

            if (count &&
                launcher_args_path(
                    launcher_entries[selected].path,
                    own_path,
                    sizeof(own_path) ))
            {
                FILE *file = fopen( own_path, "r" );

                if (file &&
                    fgets( own,
                           sizeof(own),
                           file ))
                {
                    own[strcspn(
                        own,
                        "\r\n" )] = 0;

                    if (own[0])
                        shown = own;
                }

                if (file)
                    fclose( file );
            }

            if (!shown &&
                count &&
                have_args &&
                launcher_args_match(
                    args,
                    launcher_entries[selected].dos ))
            {
                shown = args;
            }

            first = launcher_first_visible(
                first,
                selected,
                count,
                LAUNCHER_ROWS );

            launcher_draw(
                build,
                count,
                selected,
                first,
                *verbose,
                *profile,
                shown );

            redraw = 0;
        }

        svcSleepThread( 16000000 );
    }

    return 0;
}
