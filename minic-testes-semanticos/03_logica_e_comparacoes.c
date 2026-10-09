bool menor(int a, int b) {
    return a < b;
}
int principal() {
    int x;
    bool ok;
    x = 1;
    ok = menor(x, 3) && !(x == 0);
    if (ok) x = x + 1;
    return x;
}
