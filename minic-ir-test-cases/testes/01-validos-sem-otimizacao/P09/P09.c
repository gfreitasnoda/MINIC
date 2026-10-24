int main() {
    int x = 5;
    {
        int x = 8;
        print(x);
    }
    print(x);
    return 0;
}
