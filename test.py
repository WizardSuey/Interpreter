gSet: callable
gGet: callable

def main():
    a = "initial"

    def set():
        a = "updated"

    def get():
        print(a)

    gSet = set
    gGet = get

main()
gSet()
gGet()