/* Copyright (c) Jared Taylor. All Rights Reserved */

/* The only file that knows which repo this is. Copy assets/docs.css + assets/docs.js
   into another plugin, replace this, and it has a documentation site. */

window.DOCS = {
	title: 'MobWater',
	repo: 'https://github.com/Vaei/MobWater',
	icon: 'assets/icon.png',
	imgDir: 'img/',
	footer: 'MobWater is MIT licensed. &middot; <a href="shots.html">Art checklist</a>',

	sections: [
		{
			name: 'Start',
			pages: [
				{ file: 'index.html', label: 'Home', blurb: 'what this is' },
				{ file: 'install.html', label: 'Install', blurb: 'clone, generate, place a pool' }
			]
		},
		{
			name: 'Fill a world',
			pages: [
				{ file: 'bodies.html', label: 'Bodies of water', blurb: 'pool, lake, river, ocean - which one a thing is' },
				{ file: 'waves.html', label: 'Waves', blurb: 'Gerstner, baked spectrum, and the clock they share' },
				{ file: 'techniques.html', label: 'Techniques', blurb: 'depth, foam, refraction, exclusion - when and when not' },
				{ file: 'stylized.html', label: 'Stylized', blurb: 'a gradient by depth, foam in hard contours' },
				{ file: 'realistic.html', label: 'Realistic', blurb: 'absorption, crest foam, breakup' }
			]
		},
		{
			name: 'Reference',
			pages: [
				{ file: 'surface.html', label: 'Surface', blurb: 'colour, clarity, foam, every parameter' },
				{ file: 'ripples.html', label: 'Ripples', blurb: 'the field, disturbers, wakes' },
				{ file: 'exclusion.html', label: 'Exclusion', blurb: 'keeping water out of a hull' },
				{ file: 'underwater.html', label: 'Underwater', blurb: 'absorption, caustics, the surface from below' },
				{ file: 'characters.html', label: 'Characters in it', blurb: 'wading, swimming, splashing' },
				{ file: 'queries.html', label: 'Ships in it', blurb: 'the CPU query, and what it does not know' },
				{ file: 'performance.html', label: 'Cost', blurb: 'fill, instructions, permutations' },
				{ file: 'troubleshooting.html', label: 'If it is wrong', blurb: 'symptom to cause' },
				{ file: 'internals.html', label: 'Under the hood', blurb: 'the contract, regenerating, gotchas' }
			]
		},
		{
			name: 'Meta',
			pages: [
				{ file: 'changelog.html', label: 'Changelog', blurb: 'versions' },
				{ file: 'shots.html', label: 'Art checklist', blurb: 'every figure, filled or wanted' }
			]
		}
	],

	/* Figure slots. Declared once here, placed on a page by id alone.
	   A file that is not in img/ renders as a one-line placeholder instead of a gap,
	   so a page is the same length before and after the art exists.

	   Declared as the pages that place them are written, so the checker stays meaningful. */
	shots: {
		'index.hero':           { page: 'index.html', cap: 'A temple basin at dusk, a character stepping in', file: 'index-hero.png' },
		'index.tour':           { page: 'index.html', cap: 'Pool, lake, river and ocean, one material between them', file: 'index-tour.png' },
		'index.depth':          { page: 'index.html', cap: 'The same body with the bed raised: colour, clarity and foam all follow the column', file: 'index-depth.png' },
		'index.styles':         { page: 'index.html', cap: 'Stylized, toon and realistic, from the same shaders', file: 'index-styles.png' },

		'install.menu':         { page: 'install.html', cap: 'The Water menu on the level editor toolbar', file: 'install-menu.png' },
		'install.setup':        { page: 'install.html', cap: 'Set Up Water, and what it reports when it is done', file: 'install-setup.png' },
		'install.place':        { page: 'install.html', cap: 'Dragging a Water Pool out of the menu into a hollow', file: 'install-place.png' },
		'install.details':      { page: 'install.html', cap: 'A pool selected, with its extent and depth', file: 'install-details.png' },
		'install.preview':      { page: 'install.html', cap: 'Preview platform set to Android ES3.1 before judging anything', file: 'install-preview.png' },

		'bodies.pool':          { page: 'bodies.html', cap: 'Box and disc: a trough and a basin', file: 'bodies-pool.png' },
		'bodies.lake':          { page: 'bodies.html', cap: 'A closed spline, and the shoreline it was drawn as', file: 'bodies-lake.png' },
		'bodies.river':         { page: 'bodies.html', cap: 'An open spline with per-point widths', file: 'bodies-river.png' },
		'bodies.ocean':         { page: 'bodies.html', cap: 'The ocean ring, and the same swell after sailing a kilometre', file: 'bodies-ocean.png' },
		'bodies.extent':        { page: 'bodies.html', cap: 'Extent against actor scale: which one the component actually uses', file: 'bodies-extent.png' },

		'waves.presets':        { page: 'waves.html', cap: 'Pond, lake and ocean, side by side at the same scale', file: 'waves-presets.png' },
		'waves.shore':          { page: 'waves.html', cap: 'Shore attenuation off and on, at the bank', file: 'waves-shore.png', compare: 'waves-shore-b.png' },
		'waves.steepness':      { page: 'waves.html', cap: 'Choppiness from 0 to 1 across one preset', file: 'waves-steepness.png' },
		'waves.parity':         { page: 'waves.html', cap: 'Verify Contract, reporting the worst CPU-to-GPU disagreement', file: 'waves-parity.png' },
		'waves.shoal':          { page: 'waves.html', cap: 'The same swell crossing a reef, with the shoal off and on', file: 'waves-shoal.png', compare: 'waves-shoal-b.png' },
		'waves.runup':          { page: 'waves.html', cap: 'The waterline under a crest and under the trough behind it', file: 'waves-runup.png', compare: 'waves-runup-b.png' },
		'waves.surf':           { page: 'waves.html', cap: 'A surf point on a rock, and the spray a project hung off it', file: 'waves-surf.png' },
		'waves.spectrum':       { page: 'waves.html', cap: 'The five-wave Gerstner ocean, and the same ocean carrying a baked spectrum', file: 'waves-spectrum.png', compare: 'waves-spectrum-b.png' },

		'techniques.column':    { page: 'techniques.html', cap: 'The water column, visualised as the material sees it', file: 'techniques-column.png' },
		'techniques.foam':      { page: 'techniques.html', cap: 'Shoreline, edge and crest foam, each on its own', file: 'techniques-foam.png' },
		'techniques.refraction':{ page: 'techniques.html', cap: 'Refraction on and off over the same bed', file: 'techniques-refraction.png', compare: 'techniques-refraction-b.png' },
		'techniques.caustics':  { page: 'techniques.html', cap: 'Caustics on the bed, and how they are lost with depth', file: 'techniques-caustics.png' },
		'techniques.glint':     { page: 'techniques.html', cap: 'Glint threshold and density, from sheen to scattered marks', file: 'techniques-glint.png' },

		'stylized.pool':        { page: 'stylized.html', cap: 'The stylized preset on a temple pool', file: 'stylized-pool.png' },
		'stylized.bands':       { page: 'stylized.html', cap: 'One band with three quarters of it cut away', file: 'stylized-bands.png' },
		'stylized.toon':        { page: 'stylized.html', cap: 'The toon preset: flat colour, one foam line, no sky', file: 'stylized-toon.png' },

		'realistic.lake':       { page: 'realistic.html', cap: 'The realistic preset on open water', file: 'realistic-lake.png' },
		'realistic.foam':       { page: 'realistic.html', cap: 'Foam texture in the shoreline frame, running away from the bank', file: 'realistic-foam.png' },
		'realistic.detail':     { page: 'realistic.html', cap: 'Detail strength from 0 to 1, at the same distance', file: 'realistic-detail.png' },

		'surface.details':      { page: 'surface.html', cap: 'Every category on a placed body', file: 'surface-details.png' },
		'surface.opacity':      { page: 'surface.html', cap: 'Minimum opacity against clarity depth on shallow water', file: 'surface-opacity.png' },

		'ripples.field':        { page: 'ripples.html', cap: 'The ripple field, and the same view of the water', file: 'ripples-field.png' },
		'ripples.wake':         { page: 'ripples.html', cap: 'A wake, and the foam left where it has been', file: 'ripples-wake.png' },
		'ripples.reflect':      { page: 'ripples.html', cap: 'A ripple reflecting off a hull', file: 'ripples-reflect.png' },

		'exclusion.hull':       { page: 'exclusion.html', cap: 'A hull carved out of a lake, dry inside while it moves', file: 'exclusion-hull.png' },
		'exclusion.shapes':     { page: 'exclusion.html', cap: 'Disc, sphere, box and rect, and the edge each makes', file: 'exclusion-shapes.png' },
		'exclusion.softness':   { page: 'exclusion.html', cap: 'Edge softness from 0 to a metre', file: 'exclusion-softness.png' },
		'exclusion.mesh':       { page: 'exclusion.html', cap: 'A hull cut by its own outline, beside the bounding rectangle it used to be', file: 'exclusion-mesh.png' },

		'underwater.view':      { page: 'underwater.html', cap: 'Under the surface, absorbing with distance', file: 'underwater-view.png' },
		'underwater.crossing':  { page: 'underwater.html', cap: 'The waterline across the view, with the bead on it', file: 'underwater-crossing.png' },
		'underwater.caustics':  { page: 'underwater.html', cap: 'Light dappling down through the water, seen from under it', file: 'underwater-caustics.png' },
		'underwater.snell':     { page: 'underwater.html', cap: "Snell's window: the world above compressed into the cone the surface lets through", file: 'underwater-snell.png' },

		'characters.wade':      { page: 'characters.html', cap: 'Wading, with the ripples the feet leave', file: 'characters-wade.png' },
		'characters.splash':    { page: 'characters.html', cap: 'A splash bound to OnSplash, at the depth it happened', file: 'characters-splash.png' },
		'characters.swim':      { page: 'characters.html', cap: 'The swim transition, at the depth it triggers', file: 'characters-swim.png' },

		'queries.pontoons':     { page: 'queries.html', cap: 'A pontoon array, and the surface each point was answered', file: 'queries-pontoons.png' },
		'queries.debug':        { page: 'queries.html', cap: 'mob.Water.Debug, and everything the subsystem publishes', file: 'queries-debug.png' },
		'queries.buoyancy':     { page: 'queries.html', cap: 'A raft on the swell, with mob.Water.Buoyancy drawing its pontoons', file: 'queries-buoyancy.png' },

		'performance.fill':     { page: 'performance.html', cap: 'Shader complexity over a body of water', file: 'performance-fill.png' },
		'performance.report':   { page: 'performance.html', cap: 'Report Cost, in the Output Log', file: 'performance-report.png' },

		'troubleshooting.notify': { page: 'troubleshooting.html', cap: 'The missing-material notification, with the link that generates them', file: 'troubleshooting-notify.png' },
		'troubleshooting.flat':   { page: 'troubleshooting.html', cap: 'Water that renders but never moves: the collection was never written', file: 'troubleshooting-flat.png' },

		'internals.cpd':        { page: 'internals.html', cap: 'The custom primitive data a placed body is carrying', file: 'internals-cpd.png' },
		'internals.graph':      { page: 'internals.html', cap: 'The generated master, and the Custom nodes it is wired from', file: 'internals-graph.png' }
	}
};
