#include <stdio.h>
#include "help.h"
#include "theme.h"

/* Colour is chosen once at runtime: on a TTY the shared palette is used,
   otherwise every code collapses to "" so piped/redirected help stays clean. */
struct pal { const char *bold, *dim, *cyan, *gold, *silver, *reset; };

static struct pal palette(void) {
    if (th_color_for(stdout))
        return (struct pal){ TH_BOLD, TH_DIM, TH_CYAN, TH_GOLD, TH_SILVER, TH_RESET };
    return (struct pal){ "", "", "", "", "", "" };
}

static void print_section(const char *title) {
    struct pal p = palette();
    printf("%s─────────────────────────────────────────────────%s\n", p.dim, p.reset);
    printf("%s%s%s%s\n", p.bold, p.cyan, title, p.reset);
}

void print_help(void) {
    struct pal p = palette();

    printf("%s╭─────────────────────────────────────────────╮%s\n", p.silver, p.reset);
    printf("%s│%s   %s%sMnemosyne%s                                 %s│%s\n",
           p.silver, p.reset, p.bold, p.gold, p.reset, p.silver, p.reset);
    printf("%s│%s   %spersonal file search & recall%s             %s│%s\n",
           p.silver, p.reset, p.dim, p.reset, p.silver, p.reset);
    printf("%s╰─────────────────────────────────────────────╯%s\n", p.silver, p.reset);
    printf("%sSearch, browse, and launch files from anywhere.%s\n\n", p.dim, p.reset);

    print_section("USAGE");
    printf("  mn %s<command>%s [arguments]\n\n", p.cyan, p.reset);

    print_section("COMMANDS");
    printf("  %sadd%s        Index a file (.txt .md .tex .pdf)\n", p.cyan, p.reset);
    printf("  %ssearch%s     Search indexed files (add %s-c%s for case-sensitive, %s--top N%s to cap results)\n",
           p.cyan, p.reset, p.cyan, p.reset, p.cyan, p.reset);
    printf("  %slist%s       Browse all indexed files\n", p.cyan, p.reset);
    printf("  %sremove%s     Remove a file from the index (picker)\n", p.cyan, p.reset);
    printf("  %sreindex%s    Re-parse every indexed file from disk\n", p.cyan, p.reset);
    printf("  %sopen%s       Open a workspace (launch apps)\n", p.cyan, p.reset);
    printf("  %sconfig ide%s Set the default IDE\n\n", p.cyan, p.reset);

    print_section("WORKSPACES");
    printf("%s  Named sets of apps / URLs to launch together, kept in folders.%s\n", p.dim, p.reset);
    printf("  mn %sopen%s                        Pick a workspace and launch it\n", p.cyan, p.reset);
    printf("  mn %sopen edit%s                   Make, organise and edit workspaces\n\n", p.cyan, p.reset);
    printf("%s  Inside those screens, type %s/%s%s for commands:%s\n",
           p.dim, p.reset, p.cyan, p.dim, p.reset);
    /* Padded to a common column by hand: the colour codes around each command
       make printf's own %-Ns count bytes, not visible width, and get it wrong. */
    printf("    %s/create%s %s/snap%s         new workspace, in the folder you're in\n",
           p.cyan, p.reset, p.cyan, p.reset);
    printf("    %s/new-folder%s %s/move%s     organise%s (with %s/select%s for several at once)%s\n",
           p.cyan, p.reset, p.cyan, p.reset, p.dim, p.reset, p.dim, p.reset);
    printf("    %s/add%s %s/append%s          add an app, or give one a file or URL\n",
           p.cyan, p.reset, p.cyan, p.reset);
    printf("    %s/rename%s %s/delete%s       rename or remove the workspace\n",
           p.cyan, p.reset, p.cyan, p.reset);
    printf("    %s/save%s %s/back%s %s/exit%s     the ways out\n\n",
           p.cyan, p.reset, p.cyan, p.reset, p.cyan, p.reset);

    print_section("EXAMPLES");
    printf("  mn %sadd%s notes.txt\n", p.cyan, p.reset);
    printf("  mn %ssearch%s \"simplex algorithm\"\n", p.cyan, p.reset);
    printf("  mn %ssearch%s bm25 --top 5\n", p.cyan, p.reset);
    printf("  mn %sopen edit%s\n", p.cyan, p.reset);
    printf("  mn %sopen%s\n", p.cyan, p.reset);
    printf("  mn %sconfig%s ide\n", p.cyan, p.reset);
}
