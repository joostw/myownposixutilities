. "$LIB"
echo "-- a failing mailer saves the letter"; printf 'will fail\n' | FAKESEND_STATUS=75 $M -s fail a@b; echo rc=$?; awk '/^Subject/ { n = 3 } n > 0 { print; n-- }' dead.letter | norm; rm -f sent
echo "-- no recipients"; printf 'mail\n' | $M -n -f /dev/null; echo rc=$?
echo "-- -F records under the first recipient"; printf 'recorded\n' | $M -F -s f 'Some One <someone@example.com>' other@x; grep -c '^From ' someone; rm -f sent
echo "-- debug does not deliver"; printf 'set debug\n' >> rc; printf 'dbg\n' | $M -s d a@b | norm; ls sent 2>&1
echo "-- DEAD=/dev/null and nosave"; edit rc '/debug/d'; rm -f dead.letter; printf 'x\n' | DEAD=/dev/null FAKESEND_STATUS=1 $M -s f a@b; ls dead.letter 2>&1
