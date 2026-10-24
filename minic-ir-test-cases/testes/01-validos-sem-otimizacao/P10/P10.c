int fatorial(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * fatorial(n - 1);
}
int main() {
    int r;
    r = fatorial(5);
    print(r);
    return 0;
}
