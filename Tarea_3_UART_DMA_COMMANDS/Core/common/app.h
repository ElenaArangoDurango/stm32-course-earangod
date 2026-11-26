/* Core/common/app.h 
 * Author: earangod@unal.edu.co
 Es la interfaz común de las aplicaciones, capa app.c define las funciones 
 */
 // Estas líneas evitan que el contenido .h se copie dos veces en el mismo archivo
#ifndef APP_H // Aún no existe APP_H
#define APP_H // Se crea APP_H para que cuando se copie de nuevo no se replique el contenido del archivo

void app_init(void);  //  Se inicializa la aplicación ya cuando el reloj y el heartbeat están listos
void app_loop(void);  //  Se llama en el while 

#endif // Aqui se termina lo que se está procesando si la bandera APP_H  no existe 
