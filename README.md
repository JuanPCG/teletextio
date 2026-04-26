# Teletextío
Un set de programas y scripts para hacer cosas con Streams MPEG, con un enfoque en cosas de teletexto
Los emojis de oruga significan que hay algo que es muy 'buggy' o que es propenso a romperse.

## capturar.c
	Un programa que captura en una frecuencia.
		<!> Nota <!> El codigo de este programa esta 'fuertemente inspirado' por el codigo de FFMPEG y VLC, esto significa, que,
		en resumen, no tengo ni idea de lo que hace la mayoria de cosas. Pero, en teoria, y en mi maquina, funciona.
	Cambia las opciones (Adaptador) antes de compilar!
	Uso: ./capturar 581000000 (Nombre) (Archivo PID) [debug]
		(Captura todo el mux que vea en 581Mhz)
		(Mira los archivos de ejemplo de PIDs en la carpeta pids
	Usa el script TXTJSON2HTML.py para convertir el teletexto a otros formatos (HTML de momento))
	<!> ALERTA <!> Los streams tienden a producir muchos errores. Pronto subire una herramienta de correcion de errores.
	Compila este programa con 'compilar.sh', asumiendo que has construido LibZVBI con soporte para DVB y lo tienes en /usr/local/lib

## Pagina_A_Imagen.py
	Un script que saca una imagen de un archivo de teletexto usando FFMPEG (Asegurate de compilar con libzvbi)
	Uso: ./Pagina_A_Imagen.py todos.ts 800
		(Extrae del primer teletexto en 'todos.ts' la página 800)
			(Usa la herramienta ETT.py para sacar todos los teletextos como archivos individuales)

## ETT.py
	Un script que extrae todos los streams reconocidos como teletexto a archivos independientes usando FFMPEG.
	Uso: ./ETT.py 500_STREAMS.ts
		(Extrae cualquier stream de teletexto a 'teletextos-500_STREAMSts/')

## antena.c
	Un mini programa de C que 'apunta la antena' y abre el Demux para que lo veas con lo que quieras. VLC, o cat a un archivo, etc.

## sat2.c
	Pequeño programa para capturar de satelite.
