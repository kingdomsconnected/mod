import unittest

from generate_alchemy_animations import extract_events, extract_kettle_motions


class AlchemyAnimationEventsTests(unittest.TestCase):
    def test_native_markers_keep_distinct_duplicate_times(self):
        clips = {'alchemy_finish_pot': ('animations/humans/male/alchemy/alchemy_finish_pot.caf', 8.9)}
        source = b'<anim_event_list><animation name="animations/humans/male/alchemy/alchemy_finish_pot.caf"><event name="DetachPot" time="0.9"/><event name="audio_trigger" time="0.4"/></animation><animation name="animations/humans/male/alchemy/alchemy_finish_pot.caf"><event name="detachpot" time="0.9"/><event name="DetachPot" time="0.8"/></animation></anim_event_list>'
        self.assertEqual(extract_events(source, clips), [('alchemy_finish_pot', 'detachpot', 0.8), ('alchemy_finish_pot', 'detachpot', 0.9)])

    def test_invalid_authored_time_is_not_silently_clamped(self):
        clips = {'alchemy_use_mortar': ('animations/humans/male/alchemy/alchemy_use_mortar.caf', 6.5)}
        for value in ('nan', 'inf', '-0.1', '1.1'):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'Invalid normalized'):
                extract_events(('<anim_event_list><animation name="animations/humans/male/alchemy/alchemy_use_mortar.caf"><event name="Use" time="' + value + '"/></animation></anim_event_list>').encode(), clips)

    def test_obsolete_unreferenced_clips_do_not_invent_durations(self):
        self.assertEqual(extract_events(b'<anim_event_list><animation name="animations/humans/male/alchemy/alchemy_in.caf"><event name="Attach" time="0.4"/></animation></anim_event_list>', {}), [])


class AlchemyKettleMotionTests(unittest.TestCase):
    def fixture(self, moving='0.3', done='0.6'):
        clips = {name: ('animations/assets/alchemy_kettle/' + name + '.caf', 2.5) for name in ('alchemy_pot_move_up', 'alchemy_pot_move_down')}
        events = '<anim_event_list>' + ''.join('<animation name="' + path + '"><event name="moving" time="' + moving + '"/><event name="done" time="' + done + '" endTime="0.9"/></animation>' for path, duration in clips.values()) + '</anim_event_list>'
        return clips, events.encode()

    def test_object_event_start_is_used_instead_of_player_or_end_time(self):
        clips, source = self.fixture()
        rows = extract_kettle_motions(source, clips)
        self.assertEqual([row[0] for row in rows], [False, True])
        self.assertTrue(all(row[2:] == (2.5, 0.3, 0.6) for row in rows))

    def test_ambiguous_missing_or_reversed_events_refuse_generation(self):
        for moving, done in (('0.6', '0.3'), ('0.6', '0.6'), ('nan', '0.6'), ('0.3', '1.1')):
            clips, source = self.fixture(moving, done)
            with self.subTest(moving=moving, done=done), self.assertRaises(ValueError):
                extract_kettle_motions(source, clips)
        clips, source = self.fixture()
        with self.assertRaisesRegex(ValueError, 'ambiguous'):
            extract_kettle_motions(source.replace(b'<event name="done" time="0.6"', b'<event name="done" time="0.7"/><event name="done" time="0.6"', 1), clips)
        with self.assertRaisesRegex(ValueError, 'Missing'):
            extract_kettle_motions(source, {})


if __name__ == '__main__':
    unittest.main()
