bool contem(int dados[], int alvo) {
    int i;
    i = 0;
    while (i < 2) {
        if (dados[i] == alvo) return 1 < 2;
        i = i + 1;
    }
    return 2 < 1;
}
int principal() {
    int valores[2];
    bool achou;
    valores[0] = 4;
    valores[1] = 9;
    achou = contem(valores, 9);
    if (achou) return 1;
    else return 0;
}
