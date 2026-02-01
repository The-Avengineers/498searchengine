test:  ParsePage NYTimes.html NYTimes.parsed.html broken.html broken.parsed.html RandomPages.html RandomPages.parsed.html
	./ParsePage NYTimes.html > NYTimes.testout.html
	diff -Z NYTimes.parsed.html NYTimes.testout.html
	./ParsePage broken.html > broken.testout.html
	diff -Z broken.parsed.html broken.testout.html
	./ParsePage RandomPages.html > RandomPages.testout.html
	diff -Z RandomPages.parsed.html RandomPages.testout.html
	valgrind --error-exitcode=42 ./ParsePage RandomPages.html > valgrind.testout.html


ParsePage:  ParsePage.cpp HtmlTags.h HtmlTags.cpp HtmlParser.h HtmlParser.cpp ParsePage.cpp
	g++ --std=c++17 ParsePage.cpp HtmlTags.cpp HtmlParser.cpp -o ParsePage

clean:
	rm -f ParsePage NYTimes.testout.html broken.testout.html RandomPages.testout.html valgrind.testout.html
