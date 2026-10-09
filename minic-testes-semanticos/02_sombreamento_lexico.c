int principal() {
    int x;
    x = 4;
    {
        float x;
        x = 2.5;
    }
    x = x + 1;
    return x;
}
