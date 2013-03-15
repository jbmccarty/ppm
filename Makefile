ppm: ppm.c
	gcc -Wall -lasound ppm.c -o ppm

debug: ppm.c
	gcc -Wall -lasound ppm.c -g -o ppm
