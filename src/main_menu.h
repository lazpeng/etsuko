/**
 * Shows a gallery with all the available songs for the user to select
 */

#ifndef ETSUKO_MAIN_MENU_H
#define ETSUKO_MAIN_MENU_H

#include "etsuko.h"

typedef struct MainMenu_t MainMenu_t;

MainMenu_t *menu_init();
AppStatus_t menu_load_loop(MainMenu_t *menu);
void menu_setup(MainMenu_t *menu);
AppStatus_t menu_loop(MainMenu_t *menu);
void menu_finish(const MainMenu_t *menu);

#endif // ETSUKO_MAIN_MENU_H
