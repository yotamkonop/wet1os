# TODO: replace ID with your own IDs, for example: 123456789_123456789
SUBMITTERS := <student1-ID>_<student2-ID>
COMPILER := g++
COMPILER_FLAGS := --std=c++11 -Wall
SRCS := Commands.cpp signals.cpp smash.cpp
OBJS := $(subst .cpp,.o,$(SRCS))
HDRS := Commands.h signals.h
TESTS_INPUTS := $(wildcard test_input*.txt)
TESTS_OUTPUTS := $(subst input,output,$(TESTS_INPUTS))
SMASH_BIN := smash

test: $(TESTS_OUTPUTS)

$(TESTS_OUTPUTS): $(SMASH_BIN)
$(TESTS_OUTPUTS): test_output%.txt: test_input%.txt test_expected_output%.txt
	./$(SMASH_BIN) < $(word 1, $^) > $@
	diff $@ $(word 2, $^)
	echo $(word 1, $^) PASSED

$(SMASH_BIN): $(OBJS)
	$(COMPILER) $(COMPILER_FLAGS) $^ -o $@

$(OBJS): %.o: %.cpp
	$(COMPILER) $(COMPILER_FLAGS) -c $^

submit: $(SRCS) $(HDRS) Makefile
	@if ! cat /etc/os-release 2>/dev/null | grep -q "Ubuntu 18.04.4 LTS"; then \
		echo "Submission must be made from the provided image."; \
		exit 1; \
	fi
	@missing=""
	@for f in $(SRCS) $(HDRS) Makefile submitters.txt; do \
		if [ ! -f $$f ]; then \
			echo "Missing required file: $$f"; \
			missing=1; \
		fi; \
	done; \
	if [ "$$missing" = "1" ]; then \
		echo "Submission aborted. Please make sure all required files exist."; \
		exit 1; \
	fi; \
	if ! echo "$(SUBMITTERS)" | grep -Eq '^[0-9_]+$$'; then \
		echo "Please update SUBMITTERS in the Makefile with your real student ID(s)."; \
		exit 1; \
	fi
	@echo "✔ All required files found. Creating submission archive..."
	zip $(SUBMITTERS).zip $(SRCS) $(HDRS) submitters.txt Makefile
	@echo "Created: $(SUBMITTERS).zip"

clean:
	rm -rf $(SMASH_BIN) $(OBJS) $(TESTS_OUTPUTS)
	rm -rf $(SUBMITTERS).zip
