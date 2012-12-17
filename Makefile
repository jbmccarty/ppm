ppm: ppm.c
	gcc -lasound ppm.c -o ppm

debug: ppm.c
	gcc -lasound ppm.c -g -o ppm
