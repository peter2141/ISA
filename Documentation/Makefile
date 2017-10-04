FILE=manual

all:
	latex $(FILE).tex
	bibtex $(FILE)
	latex $(FILE).tex
	latex $(FILE).tex
	dvips -t a4 $(FILE).dvi	
	ps2pdf $(FILE).ps

clean:
	rm *.aux *.log *.dvi *.ps *.pdf *bbl *.blg *.toc *.out
