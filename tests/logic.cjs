const vm=require('node:vm'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const elements=new Map();function el(id){if(!elements.has(id))elements.set(id,{textContent:'',style:{},dataset:{},getBoundingClientRect:()=>({width:700,height:275}),getContext:()=>new Proxy({},{get:()=>()=>{}})});return elements.get(id)}
const ctx=vm.createContext({console,document:{getElementById:el},window:{devicePixelRatio:1,addEventListener(){}},location:{protocol:'file:'},performance:{now:()=>1000},setInterval(){},setTimeout(){}});
const html=fs.readFileSync(path.join(__dirname,'../dashboard.html'),'utf8');vm.runInContext(html.split('<script>')[1].split('</script>')[0],ctx);
const d={boot_id:'test',status:'ok',valid:true,temperature_centi:2271,age_ms:0,sample_ms:2000,uptime_ms:2000,sequence:1,errors:0,chip_id:88,error_code:0,free_heap:150000};
function send(p={}){vm.runInContext('accept('+JSON.stringify({...d,...p})+')',ctx)}
function value(s){return vm.runInContext(s,ctx)}
send();assert.match(el('temp').textContent,/22[,.]71/);send();assert.equal(value('history.length'),1);
for(let n=2;n<=320;n++)send({sequence:n,sample_ms:n*2000});assert.equal(value('history.length'),300);assert.equal(value('history.values()[0].sequence'),21);
send({status:'sensor_error',valid:false,temperature_centi:null});assert.equal(el('temp').textContent,'—');
send({status:'stale',valid:false,temperature_centi:null,age_ms:9000});assert.equal(el('state').textContent,'Lectura desactualizada');
value('online=false;render()');assert.equal(el('temp').textContent,'—');
send({boot_id:'new',temperature_centi:-123});assert.equal(value('history.length'),1);assert.match(el('temp').textContent,/-1[,.]23/);
value('received=-7000;render()');assert.equal(el('temp').textContent,'—');
assert.throws(()=>send({valid:true,temperature_centi:null}));value('history.clear();draw()');assert.equal(value('history.length'),0);
console.log('PASS: valid, duplicates, capacity, sensor error, stale, offline, reboot, negatives, local expiry, invalid JSON, clear. DOM mocked; not a visual browser test.');
