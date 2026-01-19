all:  WordCount Traverse TestWordCount TestTraverse

WordCount: WordCount.cpp
	g++ --std=c++17 WordCount.cpp -pthread -o WordCount

Traverse: Traverse.cpp
	g++ --std=c++17 Traverse.cpp -pthread -o Traverse

TestWordCount: WordCount
	./WordCount 2> WordCountUsage.testout.txt || true
	diff Documents/Tests/WordCountUsage.correct.txt WordCountUsage.testout.txt
	./WordCount Documents/Text/Shakespeare/* >  Shakespeare.testout.txt
	diff Documents/Tests/Shakespeare.correct.txt Shakespeare.testout.txt

TestTraverse: Traverse
	./Traverse 2> TraverseUsage.testout.txt || true
	diff Documents/Tests/TraverseUsage.correct.txt TraverseUsage.testout.txt
	./Traverse 5 Documents/Text Documents/Html > TraverseTextHtmlDocuments.testout.txt
	diff Documents/Tests/TraverseTextHtmlDocuments.correct.txt TraverseTextHtmlDocuments.testout.txt

	
clean:
	rm -f WordCount Traverse *.testout.txt
