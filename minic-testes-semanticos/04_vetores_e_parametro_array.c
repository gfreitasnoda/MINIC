float media(int dados[]) {
    float s;
    int i;
    s = 0;
    i = 0;
    while (i < 3) {
        s = s + dados[i];
        i = i + 1;
    }
    return s;
}
int principal() {
    int xs[3];
    float r;
    xs[0] = 1;
    xs[1] = 2;
    xs[2] = 3;
    r = media(xs);
    return 0;
}
