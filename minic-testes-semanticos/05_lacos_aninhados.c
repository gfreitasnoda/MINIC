int principal() {
    int i;
    int j;
    int total;
    i = 0;
    total = 0;
    while (i < 3) {
        j = 0;
        while (j < 2) {
            total = total + i + j;
            j = j + 1;
        }
        i = i + 1;
    }
    return total;
}
