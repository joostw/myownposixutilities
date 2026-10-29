. "$LIB"
mk <<'X'
.POSIX:
out: in
	@echo rebuilding out
X
echo x > in; echo y > out
echo "-- equal times count as out-of-date"; T 100 in out; $M
echo "-- newer target is up to date"; T 200 out; $M
echo "-- sub-second differences count"; T 300.5 in; T 300.25 out; $M; T 300.75 out; $M
echo "-- a missing prerequisite with a rule is newer"
mk m2 <<'X'
t: gen
	@echo t from $?
gen:
	@echo making gen without creating it
X
touch t; $M -f m2
echo "-- a symbolic link to its prerequisite is up to date"
printf 'view: ex\n\tln -sf ex view\n' > m3; touch ex; $M -f m3; $M -f m3
