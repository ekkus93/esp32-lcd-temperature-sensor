"""What the device tests expect of the board they run against."""

MAX_SENSORS = 5


def expected_sensor_count(expect_simulation, expect_sensors):
    """Return the exact number of sensors the snapshot must list.

    A simulation image always has five fake probes. A physical board lists the probes saved on it,
    which differs per board, so the count has to be given (`--expect-sensors`); it is never guessed.
    """
    if expect_simulation:
        if expect_sensors not in (None, MAX_SENSORS):
            raise ValueError('a simulation image always has %d sensors' % MAX_SENSORS)
        return MAX_SENSORS
    if expect_sensors is None:
        raise ValueError('give --expect-sensors: the number of probes saved on this board (0 to 5)')
    if not 0 <= expect_sensors <= MAX_SENSORS:
        raise ValueError('--expect-sensors must be 0 to %d' % MAX_SENSORS)
    return expect_sensors
