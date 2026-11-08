const MODE = {
	type: 'dropdown',
	id: 'mode',
	label: 'Action',
	default: 'toggle',
	choices: [
		{ id: 'toggle', label: 'Toggle' },
		{ id: 'on', label: 'On' },
		{ id: 'off', label: 'Off' },
	],
}

const RACK = { type: 'number', id: 'rack', label: 'Rack number', default: 1, min: 1, max: 256 }

function modeArgs(mode) {
	if (mode === 'on') return [1]
	if (mode === 'off') return [0]
	return []
}

export function getActions(self) {
	return {
		rack_bypass: {
			name: 'Rack: bypass',
			options: [RACK, MODE],
			callback: async (e) => self.send(`/liverack/rack/${Number(e.options.rack)}/bypass`, ...modeArgs(e.options.mode)),
		},
		selected_bypass: {
			name: 'Rack: bypass the selected rack',
			options: [MODE],
			callback: async (e) => self.send('/liverack/selected/bypass', ...modeArgs(e.options.mode)),
		},
		rack_select: {
			name: 'Rack: select',
			options: [RACK],
			callback: async (e) => self.send(`/liverack/rack/${Number(e.options.rack)}/select`),
		},
		plugin_bypass: {
			name: 'Plugin: bypass',
			options: [RACK, { type: 'number', id: 'slot', label: 'Plugin position (1 = top)', default: 1, min: 1, max: 64 }, MODE],
			callback: async (e) =>
				self.send(`/liverack/rack/${Number(e.options.rack)}/slot/${Number(e.options.slot)}/bypass`, ...modeArgs(e.options.mode)),
		},
		key_press: {
			name: 'User Key: press',
			options: [{ type: 'number', id: 'key', label: 'User Key', default: 1, min: 1, max: 8 }],
			callback: async (e) => self.send(`/liverack/key/${Number(e.options.key)}/press`),
		},
		hot_open: {
			name: 'Hot Plugin: open',
			options: [{ type: 'number', id: 'hot', label: 'Hot Plugin', default: 1, min: 1, max: 12 }],
			callback: async (e) => self.send(`/liverack/hot/${Number(e.options.hot)}/open`),
		},
		scene_recall: {
			name: 'Scene: recall',
			options: [{ type: 'number', id: 'scene', label: 'Scene number', default: 1, min: 0, max: 999 }],
			callback: async (e) => self.send('/liverack/scene/recall', Number(e.options.scene)),
		},
		scene_next: { name: 'Scene: next', options: [], callback: async () => self.send('/liverack/scene/next') },
		scene_prev: { name: 'Scene: previous', options: [], callback: async () => self.send('/liverack/scene/prev') },
		scene_store: {
			name: 'Scene: store (current number)',
			options: [],
			callback: async () => self.send('/liverack/scene/store'),
		},
		page_select: {
			name: 'Page: show',
			options: [{ type: 'number', id: 'page', label: 'Page (0 = All)', default: 0, min: 0, max: 64 }],
			callback: async (e) => self.send(`/liverack/page/${Number(e.options.page)}/select`),
		},
		page_next: { name: 'Page: next', options: [], callback: async () => self.send('/liverack/page/next') },
		page_prev: { name: 'Page: previous', options: [], callback: async () => self.send('/liverack/page/prev') },
		tap: { name: 'Tempo: tap', options: [], callback: async () => self.send('/liverack/tap') },
		bpm_set: {
			name: 'Tempo: set BPM',
			options: [{ type: 'number', id: 'bpm', label: 'BPM', default: 120, min: 20, max: 300, step: 0.1 }],
			callback: async (e) => self.send('/liverack/bpm', { type: 'f', value: Number(e.options.bpm) }),
		},
		two_windows: {
			name: 'View: two windows',
			options: [MODE],
			callback: async (e) => self.send('/liverack/view/twowindows', ...modeArgs(e.options.mode)),
		},
	}
}
