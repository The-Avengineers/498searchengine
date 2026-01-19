VAL = valgrind --error-exitcode=42

all:  test valgrind

test: ../Top10 ../HashTable ../HashBlob ../HashFile
	../Top10 Hamlet.txt 100 > Hamlet.Top100.testout.txt
	diff Hamlet.Top100.correct.txt Hamlet.Top100.testout.txt
	../HashTable Hamlet.txt < Top100words.txt > HashTable.testout.txt
	diff HashTable.correct.txt HashTable.testout.txt
	../HashBlob Hamlet.txt < Top100words.txt > HashBlob.testout.txt
	diff HashTable.correct.txt HashBlob.testout.txt
	../HashBlob Hamlet.txt HashBlob.testout.bin < /dev/null
	../HashFile HashBlob.testout.bin < Top100words.txt > HashFile.testout.txt
	diff HashTable.correct.txt HashFile.testout.txt

valgrind: ../Top10 ../HashTable ../HashBlob ../HashFile
	$(VAL) ../Top10 Hamlet.txt 100 > /dev/null && echo
	$(VAL) ../HashTable Hamlet.txt < Top100words.txt > /dev/null && echo
	$(VAL) ../HashBlob Hamlet.txt < Top100words.txt > /dev/null && echo
	$(VAL) ../HashBlob Hamlet.txt HashBlob.testout.bin < /dev/null && echo
	$(VAL) ../HashFile HashBlob.testout.bin < Top100words.txt > /dev/null && echo

clean:
	rm -f *.testout.*