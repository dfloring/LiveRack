// Variables follow whatever LiveRack reports: one set per rack, key, hot plugin and page
export function buildVariables(state) {
	const definitions = {
		scene: { name: 'Current scene' },
		bpm: { name: 'Tempo (BPM)' },
		selected_rack: { name: 'Selected rack name' },
		selected_rack_number: { name: 'Selected rack number' },
		page: { name: 'Current page name' },
	}

	const selected = state.racks[state.selected - 1]
	const values = {
		scene: state.scene >= 0 ? String(state.scene) : '-',
		bpm: Number.isInteger(state.bpm) ? String(state.bpm) : state.bpm.toFixed(1),
		selected_rack: selected ? selected.name : '',
		selected_rack_number: String(state.selected),
		page: state.page <= 0 ? 'All' : state.pages[state.page - 1] || '',
	}

	state.racks.forEach((r, i) => {
		const n = i + 1
		definitions[`rack_${n}_name`] = { name: `Rack ${n} name` }
		definitions[`rack_${n}_gr`] = { name: `Rack ${n} gain reduction` }
		definitions[`rack_${n}_latency`] = { name: `Rack ${n} latency (ms)` }
		definitions[`rack_${n}_group`] = { name: `Rack ${n} latency group` }
		values[`rack_${n}_name`] = r.name
		values[`rack_${n}_gr`] = r.gr >= 59 ? '-inf' : r.gr > 0.4 ? `-${r.gr.toFixed(1)}` : ''
		values[`rack_${n}_latency`] = (r.latency || 0).toFixed(2)
		values[`rack_${n}_group`] = r.group
	})

	for (let k = 1; k <= 8; k++) {
		definitions[`key_${k}_label`] = { name: `User Key ${k} label` }
		values[`key_${k}_label`] = state.keys[k - 1]?.label || ''
	}

	for (let h = 1; h <= 12; h++) {
		definitions[`hot_${h}_name`] = { name: `Hot Plugin ${h}` }
		values[`hot_${h}_name`] = state.hot[h - 1] || ''
	}

	state.pages.forEach((p, i) => {
		definitions[`page_${i + 1}_name`] = { name: `Page ${i + 1} name` }
		values[`page_${i + 1}_name`] = p
	})

	return { definitions, values }
}
