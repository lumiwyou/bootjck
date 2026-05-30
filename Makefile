all: clean compile run
compile:
	$(echo "=== Compiling ===")
	gcc -o build/bootjck bootjck.c -std=c99 -Linclude -L/usr/include
clean:
	$(echo "=== Cleaning ===")
	rm -f build/bootjck
run:
	$(echo "=== Running build ===")
	sudo ./build/bootjck /dev/sda /boot/EFI/BOOT/BOOTX64.EFI
