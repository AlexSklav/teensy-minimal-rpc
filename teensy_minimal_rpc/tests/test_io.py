def test_has_pwm(proxy):
    '''
    Check detected PWM pins match expected pins.
    '''
    # Teensy has [34 digital IO pins][1].
    #
    # [1]: https://www.pjrc.com/teensy/teensy31.html
    pwm_pins = [i for i in range(34) if proxy.digital_pin_has_pwm(i)]
    assert pwm_pins == [3, 4, 5, 6, 9, 10, 20, 21, 22, 23, 25, 32]


def test_digital_write_read(proxy):
    '''
    Check digital write/read.
    '''
    proxy.pin_mode(13, 1)

    proxy.digital_write(13, 0)
    assert proxy.digital_read(13) == 0
    proxy.digital_write(13, 1)
    assert proxy.digital_read(13) == 1
    proxy.digital_write(13, 0)
    assert proxy.digital_read(13) == 0

    proxy.pin_mode(13, 0)
