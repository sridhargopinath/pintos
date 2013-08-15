BUILD_SUBDIRS = threads userprog vm filesys

all::
	@echo "Run 'make' in subdirectories: $(BUILD_SUBDIRS)."
	@echo "This top-level make has only 'clean' targets."

CLEAN_SUBDIRS = $(BUILD_SUBDIRS) examples utils

clean::
	for d in $(CLEAN_SUBDIRS); do $(MAKE) -C $$d $@; done
	rm -f TAGS tags
	rm -f cscope.files cscope.out cscope.in.out cscope.po.out

distclean:: clean
	find . -name '*~' -exec rm '{}' \;

TAGS_SUBDIRS = $(BUILD_SUBDIRS) devices lib
TAGS_SOURCES = find $(TAGS_SUBDIRS) -name \*.[chS] -print

TAGS::
	etags --members `$(TAGS_SOURCES)`

tags::
	$(TAGS_SOURCES) | ctags -L -

cscope.files::
	$(TAGS_SOURCES) > cscope.files

cscope:: cscope.files
	cscope -b -q -k
