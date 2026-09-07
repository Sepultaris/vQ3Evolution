#include <stdio.h>
#include <assert.h>

int main(int argc, char** argv) {
    if(argc != 3 && argc != 4)
	return 1;

    char* fn = argv[1];
    FILE* f = fopen(fn, "rb");
    FILE* out = argc == 4 ? fopen(argv[3], "wb") : stdout;
    if(!f || !out)
	return 1;
    fprintf(out, "unsigned char %s[] = {\n", argv[2]);
    unsigned long n = 0;

    while(!feof(f)) {
        unsigned char c;
        if(fread(&c, 1, 1, f) == 0) break;
        if(n % 10 != 0) fprintf(out, " ");
        fprintf(out, "0x%.2X,", (int)c);
        ++n;
        if(n % 10 == 0) fprintf(out, "\n");
    }

    fclose(f);
    if(n % 10 != 0) fprintf(out, "\n");
    fprintf(out, "};\n");

    fprintf(out, "int %s_size = %ld;\n", argv[2], n);
    if(out != stdout)
	fclose(out);
    return 0;
}
